#include "UiTask.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <TFT_eSPI.h>

#include "../hw/Board.h"
#include "../input/InputTask.h"
#include "../usb/DuckyRunner.h"

namespace ui {

namespace {

constexpr int MAX_STACK = 6;

// Simple fixed-depth stack. UI depth never exceeds menu→sub→feature→keyboard.
Screen* g_stack[MAX_STACK] = {};
int     g_top = -1;

// Command queue for cross-task push/pop/replace so callers don't scribble
// directly on the stack. Every command runs on the UI task thread.
enum class CmdKind : uint8_t { Push, Pop, Replace };
struct Cmd {
    CmdKind kind;
    Screen* screen;
};
QueueHandle_t g_cmds = nullptr;

Screen* topScreen() { return g_top >= 0 ? g_stack[g_top] : nullptr; }

void applyCmd(const Cmd& c) {
    switch (c.kind) {
        case CmdKind::Push:
            if (g_top + 1 >= MAX_STACK) { delete c.screen; return; }
            if (auto* t = topScreen()) t->onExit(board::tft);
            g_stack[++g_top] = c.screen;
            c.screen->onEnter(board::tft);
            break;
        case CmdKind::Pop:
            if (g_top < 0) return;
            g_stack[g_top]->onExit(board::tft);
            delete g_stack[g_top];
            g_stack[g_top--] = nullptr;
            if (auto* t = topScreen()) {
                t->dirty();
                board::tft.fillScreen(TFT_BLACK);
                t->onEnter(board::tft); // re-enter for a fresh paint path
            }
            break;
        case CmdKind::Replace:
            while (g_top >= 0) {
                g_stack[g_top]->onExit(board::tft);
                delete g_stack[g_top];
                g_stack[g_top--] = nullptr;
            }
            board::tft.fillScreen(TFT_BLACK);
            g_stack[++g_top] = c.screen;
            c.screen->onEnter(board::tft);
            break;
    }
}

void drainCommands() {
    Cmd c;
    while (xQueueReceive(g_cmds, &c, 0) == pdTRUE) applyCmd(c);
}

// Diagnostic: when a single UI tick takes longer than this, dump a
// breakdown of which phase ate the time. 30 ms = ~2 frames at our 60 Hz
// cap, well above normal chrome refreshes. Set to 0 to disable.
constexpr uint32_t SLOW_TICK_THRESH_MS = 30;

void taskEntry(void*) {
    // Plain vTaskDelay (not vTaskDelayUntil): if a render runs long the
    // delay-until-version would "catch up" without yielding the CPU on
    // the next iteration, which can manifest as input lag because the
    // catch-up burst keeps the input queue idle for an extra frame.
    constexpr TickType_t tick = pdMS_TO_TICKS(16); // ~60 Hz cap

    Serial.println("[ui-task] entered");
    Serial.flush();

    uint32_t lastWakeMs = millis();
    uint32_t lastHbMs   = lastWakeMs;
    uint32_t hbTicks    = 0;
    uint32_t hbMaxFrame = 0;

    for (;;) {
        uint32_t t0 = millis();
        // Inter-tick gap = how long FreeRTOS held our task off-CPU since
        // last loop. If this is high, something else (USB/WiFi/BLE stack)
        // is preempting us on core 0.
        uint32_t gap = t0 - lastWakeMs;

        drainCommands();
        uint32_t t1 = millis();

        // Drain input events (non-blocking, all in one tick).
        input::Event e;
        int evCount = 0;
        while (xQueueReceive(input::queue(), &e, 0) == pdTRUE) {
            if (auto* s = topScreen()) s->onEvent(e);
            ++evCount;
        }
        uint32_t t2 = millis();

        if (auto* s = topScreen()) {
            s->onTick(millis());
        }
        uint32_t t3 = millis();

        bool rendered = false;
        if (auto* s = topScreen()) {
            if (s->consumeDirty()) { s->onRender(board::tft); rendered = true; }
        }
        uint32_t t4 = millis();

        // Globally poll the Ducky runner so an armed script fires on
        // host-connect no matter which screen is on top. Cheap when not
        // armed — just returns false immediately.
        usb::ducky::maybeRun();
        uint32_t t5 = millis();

        uint32_t total = t5 - t0;
        if (SLOW_TICK_THRESH_MS && total >= SLOW_TICK_THRESH_MS) {
            Serial.printf("[ui-slow] total=%lums gap=%lums cmds=%lu "
                          "ev(%d)=%lu tick=%lu render(%d)=%lu ducky=%lu\n",
                          (unsigned long)total,
                          (unsigned long)gap,
                          (unsigned long)(t1 - t0),
                          evCount, (unsigned long)(t2 - t1),
                          (unsigned long)(t3 - t2),
                          (int)rendered, (unsigned long)(t4 - t3),
                          (unsigned long)(t5 - t4));
        }

        // Heartbeat once every 2 s: tick rate + worst single-frame time
        // observed in that window. A healthy UI shows ticks≈120 in 2 s
        // with maxFrame<5 ms. Anything dramatically off means either the
        // task is being preempted or a render is running long.
        ++hbTicks;
        if (total > hbMaxFrame) hbMaxFrame = total;
        uint32_t nowMs = millis();
        if (nowMs - lastHbMs >= 2000) {
            Serial.printf("[ui-hb] %lus ticks=%lu max=%lums\n",
                          (unsigned long)(nowMs / 1000),
                          (unsigned long)hbTicks,
                          (unsigned long)hbMaxFrame);
            Serial.flush();
            hbTicks    = 0;
            hbMaxFrame = 0;
            lastHbMs   = nowMs;
        }

        lastWakeMs = nowMs;
        vTaskDelay(tick);
    }
}

} // namespace

void start() {
    if (g_cmds) return;
    g_cmds = xQueueCreate(8, sizeof(Cmd));
    // UI on core 0 at priority 2 — the standard "user task" priority on
    // Arduino-ESP32. Bumping it to 4 (an experiment during the lag-hunt)
    // turned out to interact badly with the IDF system tasks that live on
    // core 0 and feed peripheral drivers; it's the wrong knob to fix
    // input lag.
    xTaskCreatePinnedToCore(taskEntry, "ui", 8192, nullptr,
                            /*prio=*/2, nullptr, /*coreId=*/0);
}

void push(Screen* s)    { Cmd c{CmdKind::Push,    s}; xQueueSend(g_cmds, &c, 0); }
void pop()              { Cmd c{CmdKind::Pop,     nullptr}; xQueueSend(g_cmds, &c, 0); }
void replace(Screen* s) { Cmd c{CmdKind::Replace, s}; xQueueSend(g_cmds, &c, 0); }

void repaintTop() {
    auto* t = topScreen();
    if (!t) return;
    board::tft.fillScreen(TFT_BLACK);
    t->onEnter(board::tft);   // forces fresh layout + palette pickup
}

} // namespace ui
