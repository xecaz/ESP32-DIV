#include "InputTask.h"

#include <Arduino.h>
#include <Wire.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <driver/periph_ctrl.h>
#include <soc/i2c_struct.h>

#include "../hw/Pins.h"
#include "../storage/Settings.h"

// Small, explicit PCF8574 read: we control the error path (unlike the xreef
// library, which returns a stale cached value on bus failure — visible as
// "all buttons pressed at once" since unpulled-up inputs read 0).

namespace input {

namespace {

constexpr int NUM_KEYS = 5;

struct KeyDef {
    Key      key;
    uint8_t  pcfBit;
};

const KeyDef KEYS[NUM_KEYS] = {
    { Key::Up,     pins::BTN_UP     },
    { Key::Down,   pins::BTN_DOWN   },
    { Key::Left,   pins::BTN_LEFT   },
    { Key::Right,  pins::BTN_RIGHT  },
    { Key::Select, pins::BTN_SELECT },
};

struct KeyState {
    bool     stable     = false;     // debounced state (true = pressed)
    uint8_t  pressStreak = 0;        // consecutive polls reading "pressed"
    uint8_t  releaseStreak = 0;      // consecutive polls reading "released"
    uint32_t nextRepeat = 0;         // when to fire next KeyRepeat
};

Tuning        g_tuning;
QueueHandle_t g_queue = nullptr;
KeyState      g_keys[NUM_KEYS];

// Plain Wire-based PCF read. The bit-bang attempt produced corrupted
// reads (presses misregistering as wrong direction) on this board;
// Wire's slow but correct.
bool readPcfRaw(uint8_t* out) {
    if (Wire.requestFrom((int)pins::PCF_I2C_ADDR, 1) != 1) return false;
    *out = Wire.read();
    return true;
}

// Latest PCF state, populated by a dedicated I²C poller task running on
// the OPPOSITE core from the input task. The input loop reads this byte
// (atomic 8-bit access on Xtensa) and never blocks on Wire — so when the
// IDF i2c driver inevitably stalls 1 s on bus-busy, only the poller is
// frozen, not the input pipeline or the UI.
//
// Initialised to 0xFF (all bits high = no buttons pressed) so we don't
// fire phantom events before the first read completes.
volatile uint8_t  g_latestPcfRaw    = 0xFF;
volatile uint32_t g_latestPcfReadMs = 0;
volatile uint32_t g_pcfPollOk       = 0;
volatile uint32_t g_pcfPollFail     = 0;
volatile uint32_t g_pcfRecoverCount = 0;

// Stale-data threshold. If the cached PCF byte is older than this, the
// input loop freezes its debounce/repeat state. Tighter is better for
// responsiveness — 30 ms covers normal poll jitter + recovery time but
// still trips quickly when the bus actually stalls, preventing run-away
// repeats from stale "pressed" data.
constexpr uint32_t PCF_STALE_THRESH_MS = 30;

// Direct read of the I²C peripheral status register. The IDF tracks
// "bus busy" here; when it's set, a Wire.requestFrom would block ~1 s
// in `i2c_master_cmd_begin`'s wait-for-busy-clear loop. Reading the
// register is one memory load — much faster than discovering the busy
// state by getting stuck inside Wire.
inline bool idfBusBusy() {
    return I2C0.sr.bus_busy != 0;
}

// Heavy-handed I²C peripheral recovery. We've tried gentler approaches
// (just toggling SCL, just Wire.end+begin) and they don't reliably clear
// the IDF's internal busy flag once it gets stuck. periph_module_reset
// power-cycles the I²C peripheral at the silicon level, which definitely
// resets every flag.
void pcfRecoverBus() {
    // Detach pins from any peripheral.
    pinMode(pins::I2C_SDA, INPUT_PULLUP);
    pinMode(pins::I2C_SCL, OUTPUT_OPEN_DRAIN);
    digitalWrite(pins::I2C_SCL, HIGH);
    delayMicroseconds(10);

    // 9 SCL pulses — enough to clock a stuck slave through to a NAK
    // and release SDA, regardless of where in a transaction it stuck.
    for (int i = 0; i < 9; ++i) {
        digitalWrite(pins::I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(pins::I2C_SCL, HIGH);
        delayMicroseconds(5);
        if (digitalRead(pins::I2C_SDA) == HIGH) break;  // slave released
    }

    // Manual STOP condition: SDA low → high while SCL high.
    pinMode(pins::I2C_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(pins::I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(pins::I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(pins::I2C_SDA, HIGH);
    delayMicroseconds(10);

    // Power-cycle the I²C peripheral. This clears the IDF's bus-busy
    // flag at the silicon level — Wire.end() alone often doesn't.
    Wire.end();
    periph_module_reset(PERIPH_I2C0_MODULE);
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, /*freq=*/100000);
    Wire.setTimeOut(50);
}

void pcfPollerTask(void*) {
    // Mark the cache as freshly initialised so the input task isn't
    // stuck waiting for the first successful read at boot.
    g_latestPcfReadMs = millis();
    uint32_t lastOkMs = millis();
    for (;;) {
        // Pre-check: read the IDF's bus-busy bit AND the physical
        // line state. Either being "wedged" means a Wire.requestFrom
        // would block ~1 s in the IDF's wait-for-busy-clear loop.
        // Skipping is a single memory load; recovery is ~150 µs. Way
        // cheaper than letting the call stall.
        if (idfBusBusy() ||
            digitalRead(pins::I2C_SDA) == LOW ||
            digitalRead(pins::I2C_SCL) == LOW) {
            pcfRecoverBus();
            g_pcfRecoverCount++;
            lastOkMs = millis();
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        uint8_t raw;
        if (readPcfRaw(&raw)) {
            g_latestPcfRaw    = raw;
            g_latestPcfReadMs = millis();
            g_pcfPollOk++;
            lastOkMs = millis();
        } else {
            g_pcfPollFail++;
            // Wire failed but bus didn't look stuck — IDF's internal
            // bus-busy flag may be set even though SDA/SCL are high.
            // Recover proactively after a short fail streak so we don't
            // accumulate stalled calls.
            if (millis() - lastOkMs > 50) {
                pcfRecoverBus();
                g_pcfRecoverCount++;
                lastOkMs = millis();
            }
        }
        // 1 ms cadence when bus is healthy = ~500 reads/sec, freshness
        // window <2 ms, STREAK=4 debounce ≈ 4 ms KeyDown latency.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Bus-recovery is currently disabled: when active it actually made things
// worse — Wire.end() / Wire.begin() each register an APB-change callback
// internally, and stacking those over time was producing spurious resets
// of unrelated peripheral clocks. Failed reads are now just skipped (the
// streak debounce in scanKeys absorbs them).
void maybeRecoverBus(bool /*success*/) {}

// Mask of bits actually wired to buttons; everything else is forced high so
// garbage on unused lines never looks like a press.
constexpr uint8_t BUTTON_MASK =
    (1u << pins::BTN_UP)    | (1u << pins::BTN_DOWN) |
    (1u << pins::BTN_LEFT)  | (1u << pins::BTN_RIGHT) |
    (1u << pins::BTN_SELECT);

// Read the cached PCF byte populated by pcfPollerTask. Apply two
// filters before trusting the value:
//   1. Freshness: cache must be ≤ PCF_STALE_THRESH_MS old.
//   2. Popcount: at most one button bit may be low at a time.
//
// (An earlier "unused bits 0-2 must read as 1" invariant turned out to
//  reject ALL reads on this board — those bits don't reliably stay
//  high, possibly because they're wired to something undocumented.
//  Removed.)
bool readPcf(uint8_t* out) {
    uint32_t age = millis() - g_latestPcfReadMs;
    if (age > PCF_STALE_THRESH_MS) return false;
    uint8_t r = g_latestPcfRaw;
    uint8_t raw = r | (uint8_t)~BUTTON_MASK;   // force unused bits to 1
    if (__builtin_popcount((uint8_t)~raw) > 1) return false;
    *out = raw;
    return true;
}

SPIClass          g_touchSpi(HSPI);
XPT2046_Touchscreen g_touch(pins::TOUCH_CS_PIN);
bool              g_touchActive = false;
volatile int16_t  g_rawX = 0, g_rawY = 0;
volatile uint16_t g_rawZ = 0;
volatile uint32_t g_rawMs = 0;

// Track dropped events so we can see if the queue is overflowing — that
// would mean the UI task isn't draining fast enough.
volatile uint32_t g_dropped = 0;

void pushEvent(const Event& e) {
    if (!g_queue) return;
    // Non-blocking; if queue is full, drop (input events are disposable).
    if (xQueueSend(g_queue, &e, 0) != pdTRUE) g_dropped++;
}

uint32_t droppedCount() { return g_dropped; }

void scanKeys(uint32_t now) {
    uint8_t raw;
    bool ok = readPcf(&raw);
    maybeRecoverBus(ok);
    if (!ok) return; // bus hiccup — skip this tick, no spurious events

    // Streak-based debounce: require N consecutive polls of the same state
    // before flipping `stable`. Bumped from 2 to 4 to filter the I²C bus
    // glitches on this board — corrupt single-byte reads were sneaking
    // through the popcount filter when their random low bit happened to
    // line up with a button bit, producing ghost presses. At 5 ms poll
    // cadence, STREAK=4 = 20 ms latency for KeyDown, still imperceptible.
    constexpr uint8_t STREAK = 3;

    for (int i = 0; i < NUM_KEYS; ++i) {
        bool pressed = !((raw >> KEYS[i].pcfBit) & 1u);
        KeyState& s = g_keys[i];

        if (pressed) {
            if (s.pressStreak < 0xFF) s.pressStreak++;
            s.releaseStreak = 0;
        } else {
            if (s.releaseStreak < 0xFF) s.releaseStreak++;
            s.pressStreak = 0;
        }

        if (!s.stable && s.pressStreak >= STREAK) {
            s.stable = true;
            pushEvent({ EventType::KeyDown, KEYS[i].key, 0, 0, 0, now });
            s.nextRepeat = now + g_tuning.longPressMs;
        } else if (s.stable && s.releaseStreak >= STREAK) {
            s.stable = false;
            pushEvent({ EventType::KeyUp, KEYS[i].key, 0, 0, 0, now });
        }

        // Auto-repeat while held.
        if (s.stable && (int32_t)(now - s.nextRepeat) >= 0) {
            pushEvent({ EventType::KeyRepeat, KEYS[i].key, 0, 0, 0, now });
            s.nextRepeat = now + g_tuning.repeatMs;
        }
    }
}

// Map raw XPT2046 coords to pixel space (240x320) using the calibration
// bounds AND orientation flags detected by the 4-corner Touch Calibrate
// screen. The flags let the same mapping handle axis-swapped and/or
// mirrored panels without hard-coding a rotation assumption.
static void rawToPixel(int rawX, int rawY, int16_t& outX, int16_t& outY) {
    const auto& s = storage::get();

    // Axis swap: if the panel's raw X actually drives screen Y, route
    // accordingly. Otherwise raw X → screen X.
    int srcForScreenX = s.touchSwapXY ? rawY : rawX;
    int srcForScreenY = s.touchSwapXY ? rawX : rawY;
    uint16_t xMin = s.touchSwapXY ? s.touchYMin : s.touchXMin;
    uint16_t xMax = s.touchSwapXY ? s.touchYMax : s.touchXMax;
    uint16_t yMin = s.touchSwapXY ? s.touchXMin : s.touchYMin;
    uint16_t yMax = s.touchSwapXY ? s.touchXMax : s.touchYMax;

    int tx = map(srcForScreenX, xMin, xMax, 0, 239);
    int ty = map(srcForScreenY, yMin, yMax, 0, 319);
    if (tx < 0)   tx = 0; else if (tx > 239) tx = 239;
    if (ty < 0)   ty = 0; else if (ty > 319) ty = 319;

    outX = (int16_t)(s.touchInvertX ? 239 - tx : tx);
    outY = (int16_t)(s.touchInvertY ? 319 - ty : ty);
}

void scanTouch(uint32_t now) {
    if (!g_touch.tirqTouched() && !g_touchActive) return;

    // Hysteresis + post-release lockout: bounce-free single TouchDown per
    // real tap. Earlier code would re-fire TouchDown whenever pressure
    // briefly dipped below the threshold mid-press (very common on noisy
    // resistive panels), giving the "red dots all over the screen"
    // symptom.
    //   - Z must exceed ENTER_Z to transition released→pressed
    //   - While pressed, any Z above STAY_Z keeps us in pressed state
    //   - After release, ignore touch events for LOCKOUT_MS
    constexpr uint16_t ENTER_Z    = 700;
    constexpr uint16_t STAY_Z     = 200;
    constexpr uint32_t LOCKOUT_MS = 60;
    static uint32_t releasedAtMs = 0;

    TS_Point p = g_touch.getPoint();
    g_rawX  = (int16_t)p.x;
    g_rawY  = (int16_t)p.y;
    g_rawZ  = (uint16_t)p.z;
    g_rawMs = now;

    bool rawTouched = g_touch.touched();

    if (g_touchActive) {
        // Stay pressed only while either the library still sees contact
        // OR the pressure is above the "stay" floor.
        if (rawTouched && p.z >= STAY_Z) {
            int16_t px, py;
            rawToPixel(p.x, p.y, px, py);
            pushEvent({ EventType::TouchMove, Key::None, px, py,
                        (uint16_t)p.z, now });
        } else {
            pushEvent({ EventType::TouchUp, Key::None, 0, 0, 0, now });
            g_touchActive = false;
            releasedAtMs  = now;
        }
    } else {
        if (rawTouched && p.z >= ENTER_Z &&
            (now - releasedAtMs) >= LOCKOUT_MS) {
            int16_t px, py;
            rawToPixel(p.x, p.y, px, py);
            pushEvent({ EventType::TouchDown, Key::None, px, py,
                        (uint16_t)p.z, now });
            g_touchActive = true;
        }
    }
}

// Touchscreen polling. Disabled during the early keyboard-debounce work;
// the key pipeline is now reliable, so re-enabling for touch-UI testing.
constexpr bool TOUCH_ENABLED = true;

void taskEntry(void*) {
    TickType_t period = pdMS_TO_TICKS(g_tuning.pollPeriodMs);
    TickType_t last   = xTaskGetTickCount();
    uint32_t   nextTouchMs = 0;

    // Diagnostic: log every poll period > 20 ms (target is 2 ms). High
    // values mean this task is being starved off-CPU by something else
    // on core 1.
    constexpr uint32_t SLOW_POLL_MS = 20;
    uint32_t lastPollMs = millis();
    uint32_t lastDropReport = 0;
    uint32_t lastDropCount  = 0;

    for (;;) {
        uint32_t now = millis();
        uint32_t poll_gap = now - lastPollMs;
        if (poll_gap >= SLOW_POLL_MS) {
            Serial.printf("[input-slow] poll gap=%lums\n",
                          (unsigned long)poll_gap);
        }
        lastPollMs = now;

        scanKeys(now);
        if (TOUCH_ENABLED && (int32_t)(now - nextTouchMs) >= 0) {
            scanTouch(now);
            nextTouchMs = now + g_tuning.touchPollMs;
        }

        // Once a second, if any events were dropped since the last
        // report, log how many. Silent when nothing was dropped.
        if (now - lastDropReport >= 1000) {
            uint32_t d = g_dropped;
            if (d != lastDropCount) {
                Serial.printf("[input-drop] queue dropped %lu since last report\n",
                              (unsigned long)(d - lastDropCount));
                lastDropCount = d;
            }
            lastDropReport = now;
        }

        vTaskDelayUntil(&last, period);
    }
}

} // namespace

Tuning&        tuning() { return g_tuning; }
QueueHandle_t  queue()  { return g_queue; }

RawTouch lastRawTouch() {
    return RawTouch{ g_rawX, g_rawY, g_rawZ, g_rawMs };
}

PcfStats pcfStats() {
    PcfStats s{};
    s.okCount       = g_pcfPollOk;
    s.failCount     = g_pcfPollFail;
    s.recoverCount  = g_pcfRecoverCount;
    s.lastOkAgeMs   = millis() - g_latestPcfReadMs;
    s.latestRaw     = g_latestPcfRaw;
    return s;
}

void resetPcfStats() {
    g_pcfPollOk       = 0;
    g_pcfPollFail     = 0;
    g_pcfRecoverCount = 0;
}

void start() {
    if (g_queue) return;  // idempotent
    g_queue = xQueueCreate(16, sizeof(Event));

    // PCF8574 bring-up: write 0xFF so every bit is high (quasi-bidirectional
    // input mode).
    // PCF8574 bring-up: write 0xFF so every bit is high (quasi-bidirectional
    // input mode).
    Wire.beginTransmission(pins::PCF_I2C_ADDR);
    Wire.write(0xFF);
    Wire.endTransmission();

    // XPT2046 on HSPI shared with TFT (TFT_eSPI already owns the bus; the
    // touchscreen library internally uses SPI.beginTransaction() so they
    // coexist as long as neither holds the bus across calls).
    g_touchSpi.begin(pins::TOUCH_SCLK_PIN, pins::TOUCH_MISO_PIN,
                     pins::TOUCH_MOSI_PIN, pins::TOUCH_CS_PIN);
    g_touch.begin(g_touchSpi);

    // Run input on core 1 so UI rendering on core 0 (TFT_eSPI fillRect can
    // take 10-20 ms per frame) can't preempt button polling. The actual
    // I²C reads happen in the dedicated poller below — pinned to core 0,
    // away from the input task — so when the IDF i2c driver stalls for ~1 s
    // on bus-busy, only the poller is frozen and the input pipeline keeps
    // pulling fresh bytes (the cached value just goes a bit stale).
    xTaskCreatePinnedToCore(taskEntry, "input", 4096, nullptr,
                            /*prio=*/3, nullptr, /*coreId=*/1);
    // Poller at high priority on core 0 so it preempts UI rendering, USB
    // CDC, and other peripherals when it's ready to run. Each iteration
    // is short (<1 ms when bus is healthy, otherwise immediately
    // recovers and yields), so the preemption doesn't visibly affect UI.
    xTaskCreatePinnedToCore(pcfPollerTask, "pcf_poll", 4096, nullptr,
                            /*prio=*/5, nullptr, /*coreId=*/0);
}

const char* keyName(Key k) {
    switch (k) {
        case Key::Up:     return "UP";
        case Key::Down:   return "DOWN";
        case Key::Left:   return "LEFT";
        case Key::Right:  return "RIGHT";
        case Key::Select: return "SEL";
        case Key::None:   return "-";
    }
    return "?";
}

const char* typeName(EventType t) {
    switch (t) {
        case EventType::KeyDown:    return "DOWN";
        case EventType::KeyUp:      return "UP";
        case EventType::KeyRepeat:  return "RPT";
        case EventType::TouchDown:  return "TDN";
        case EventType::TouchMove:  return "TMV";
        case EventType::TouchUp:    return "TUP";
    }
    return "?";
}

} // namespace input
