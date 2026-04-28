#include "BleScan.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/BleDriver.h"

namespace ui {

namespace {
constexpr int ROW_H  = 26;
constexpr int LIST_Y = 56;
constexpr int LIST_HEIGHT = 240;
constexpr int VISIBLE_ROWS = LIST_HEIGHT / ROW_H;
} // namespace

void BleScanScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "BLE Scan");

    cursor_ = 0;
    scrollTop_ = 0;
    lastRenderCount_ = 0;
    startedMs_ = millis();
    radio::ble::clearResults();
    radio::ble::startScan();
    dirty();
}

void BleScanScreen::onExit(TFT_eSPI&) {
    radio::ble::stop();
}

bool BleScanScreen::onEvent(const input::Event& e) {
    using input::EventType;
    using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    int n = radio::ble::resultCount();
    switch (e.key) {
        case Key::Up:
            if (n == 0) return true;
            cursor_ = (cursor_ - 1 + n) % n;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE_ROWS)
                scrollTop_ = cursor_ - VISIBLE_ROWS + 1;
            dirty();
            return true;
        case Key::Down:
            if (n == 0) return true;
            cursor_ = (cursor_ + 1) % n;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE_ROWS)
                scrollTop_ = cursor_ - VISIBLE_ROWS + 1;
            dirty();
            return true;
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Select:
        case Key::Right:
            if (e.type == EventType::KeyDown) {
                // Clear and re-scan.
                radio::ble::clearResults();
                startedMs_ = millis();
                cursor_    = 0;
                scrollTop_ = 0;
                dirty();
            }
            return true;
        default: return false;
    }
}

void BleScanScreen::onTick(uint32_t nowMs) {
    // Repaint if the count changed or once per second for the elapsed timer.
    int n = radio::ble::resultCount();
    if ((uint32_t)n != lastRenderCount_) {
        lastRenderCount_ = n;
        dirty();
    }
    static uint32_t lastSec = 0;
    if (nowMs - lastSec >= 1000) {
        lastSec = nowMs;
        dirty();
    }
}

void BleScanScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    int n = radio::ble::resultCount();

    // Status line.
    tft.fillRect(0, 30, 240, 24, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 34);
    uint32_t elapsed = millis() - startedMs_;
    tft.printf("%d devs   %lus", n, (unsigned long)(elapsed / 1000));

    // List area.
    tft.fillRect(0, LIST_Y, 240, LIST_HEIGHT, p.bg);
    for (int vi = 0; vi < VISIBLE_ROWS; ++vi) {
        int i = scrollTop_ + vi;
        if (i >= n) break;
        radio::BleEntry e;
        if (!radio::ble::resultAt(i, e)) continue;

        int y = LIST_Y + vi * ROW_H;
        bool sel = (i == cursor_);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.textDim;
        tft.fillRect(0, y, 240, ROW_H - 2, bg);
        tft.setTextColor(fg, bg);
        tft.setCursor(4, y + 2);
        String name = e.name.length() ? e.name : String("<no name>");
        if (name.length() > 16) { name = name.substring(0, 16); name += "…"; }
        tft.print(name);

        tft.setTextFont(1);
        tft.setTextColor(sel ? TFT_YELLOW : p.textDim, bg);
        tft.setCursor(4, y + 14);
        tft.printf("%02X:%02X:%02X:%02X:%02X:%02X %dd",
                   e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5],
                   e.rssi);
        tft.setCursor(170, y + 14);
        tft.printf("x%u", e.packetCount);
        tft.setTextFont(2);
    }

    theme::drawFooter(tft, "SEL = clear+rescan  LEFT = back");
}

} // namespace ui
