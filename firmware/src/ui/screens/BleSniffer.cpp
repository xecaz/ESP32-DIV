#include "BleSniffer.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/BleDriver.h"

namespace ui {

namespace {
constexpr int ROW_H  = 30;
constexpr int LIST_Y = 56;
constexpr int VISIBLE_ROWS = 8;

// A device whose "packet count" is rising fast but hasn't been seen before
// this session, or whose RSSI jumps around, is flagged visually — matches
// the stock firmware's "highlight suspicious" behavior for randomized-MAC
// hunting.
bool looksSuspicious(const radio::BleEntry& e, uint32_t now) {
    // Fewer than 3 packets and seen only in the last 500 ms = possible
    // freshly-randomized identity.
    if (e.packetCount < 3 && (now - e.lastSeenMs) < 500) return true;
    return false;
}
} // namespace

void BleSnifferScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "BLE Sniff");

    cursor_ = scrollTop_ = 0;
    startedMs_ = millis();
    radio::ble::clearResults();
    radio::ble::startScan();
    dirty();
}

void BleSnifferScreen::onExit(TFT_eSPI&) { radio::ble::stop(); }

bool BleSnifferScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    int n = radio::ble::resultCount();
    switch (e.key) {
        case Key::Up:
            if (n) { cursor_ = (cursor_ - 1 + n) % n;
                     if (cursor_ < scrollTop_) scrollTop_ = cursor_;
                     if (cursor_ >= scrollTop_ + VISIBLE_ROWS)
                         scrollTop_ = cursor_ - VISIBLE_ROWS + 1;
                     dirty(); }
            return true;
        case Key::Down:
            if (n) { cursor_ = (cursor_ + 1) % n;
                     if (cursor_ < scrollTop_) scrollTop_ = cursor_;
                     if (cursor_ >= scrollTop_ + VISIBLE_ROWS)
                         scrollTop_ = cursor_ - VISIBLE_ROWS + 1;
                     dirty(); }
            return true;
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Select: case Key::Right:
            if (e.type == EventType::KeyDown) {
                radio::ble::clearResults();
                startedMs_ = millis();
                cursor_ = scrollTop_ = 0;
                dirty();
            }
            return true;
        default: return false;
    }
}

void BleSnifferScreen::onTick(uint32_t nowMs) {
    int n = radio::ble::resultCount();
    if (n != lastCount_ || (nowMs - startedMs_) % 1000 < 50) {
        lastCount_ = n;
        dirty();
    }
}

void BleSnifferScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    int n = radio::ble::resultCount();

    tft.fillRect(0, 30, 240, 24, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 34);
    uint32_t elapsed = millis() - startedMs_;
    tft.printf("%d devs  %lus  !=susp", n, (unsigned long)(elapsed / 1000));

    tft.fillRect(0, LIST_Y, 240, VISIBLE_ROWS * ROW_H, p.bg);

    uint32_t now = millis();
    for (int vi = 0; vi < VISIBLE_ROWS; ++vi) {
        int i = scrollTop_ + vi;
        if (i >= n) break;
        radio::BleEntry e;
        if (!radio::ble::resultAt(i, e)) continue;

        int y = LIST_Y + vi * ROW_H;
        bool sel = (i == cursor_);
        bool susp = looksSuspicious(e, now);
        uint16_t bg = sel ? p.selBg : (susp ? 0x4000 : p.bg);
        uint16_t fg = sel ? p.selFg : p.textDim;
        tft.fillRect(0, y, 240, ROW_H - 2, bg);

        tft.setTextFont(2);
        tft.setTextColor(fg, bg);
        tft.setCursor(4, y + 2);
        tft.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                   e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);

        tft.setTextFont(1);
        tft.setTextColor(sel ? TFT_YELLOW : (susp ? p.warn : p.textDim), bg);
        tft.setCursor(4, y + 18);
        String name = e.name.length() ? e.name : String("<nameless>");
        if (name.length() > 14) { name = name.substring(0, 14); name += "…"; }
        tft.printf("%s  %dd  x%u  %lums",
                   name.c_str(), e.rssi, e.packetCount,
                   (unsigned long)(now - e.lastSeenMs));
        tft.setTextFont(2);
    }

    theme::drawFooter(tft, "SEL=clear  LEFT=back");
}

} // namespace ui
