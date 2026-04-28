#include "WifiScan.h"

#include <TFT_eSPI.h>
#include <WiFi.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/WifiDriver.h"

namespace ui {

namespace {
constexpr int ROW_H  = 24;
constexpr int LIST_Y = 36;
constexpr int LIST_HEIGHT = 260;  // before the footer hint
constexpr int VISIBLE_ROWS = LIST_HEIGHT / ROW_H;

const char* encName(uint8_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN:           return "OPEN";
        case WIFI_AUTH_WEP:            return "WEP";
        case WIFI_AUTH_WPA_PSK:        return "WPA";
        case WIFI_AUTH_WPA2_PSK:       return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE:return "EAP";
        case WIFI_AUTH_WPA3_PSK:       return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2/3";
        default:                       return "?";
    }
}
} // namespace

void WifiScanScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Wi-Fi Scan");

    cursor_ = 0;
    scrollTop_ = 0;
    lastResults_ = -1;
    lastScanStartMs_ = millis();
    scanActive_ = radio::wifi::startScan();
    dirty();
}

void WifiScanScreen::onExit(TFT_eSPI&) {
    radio::wifi::stop();
    scanActive_ = false;
}

bool WifiScanScreen::onEvent(const input::Event& e) {
    using input::EventType;
    using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    int n = radio::wifi::resultCount();
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
        case Key::Right:
        case Key::Select:
            if (e.type == EventType::KeyDown) {
                // Rescan on select.
                lastScanStartMs_ = millis();
                scanActive_ = radio::wifi::startScan();
                dirty();
            }
            return true;
        default: return false;
    }
}

void WifiScanScreen::onTick(uint32_t nowMs) {
    bool done = radio::wifi::scanDone();
    if (done && scanActive_) {
        scanActive_ = false;
        dirty();
    }
    // Animate the "scanning…" indicator.
    if (scanActive_ && (nowMs - lastScanStartMs_) % 400 < 50) {
        dirty();
    }
}

void WifiScanScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    int n = radio::wifi::resultCount();

    // Status line under the header.
    tft.fillRect(0, 30, 240, 20, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 32);
    if (scanActive_) {
        uint32_t elapsed = millis() - lastScanStartMs_;
        tft.printf("scanning... %lus", (unsigned long)(elapsed / 1000));
    } else {
        tft.printf("%d networks", n);
    }

    // Full redraw of the list area on every render (cheap enough at 32 rows).
    tft.fillRect(0, LIST_Y + 12, 240, LIST_HEIGHT, p.bg);

    for (int vi = 0; vi < VISIBLE_ROWS; ++vi) {
        int i = scrollTop_ + vi;
        if (i >= n) break;
        radio::ScanEntry e;
        if (!radio::wifi::resultAt(i, e)) continue;

        int y = LIST_Y + 12 + vi * ROW_H;
        bool sel = (i == cursor_);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.textDim;
        tft.fillRect(0, y, 240, ROW_H - 2, bg);
        tft.setTextColor(fg, bg);
        tft.setCursor(4, y + 2);
        // Truncate SSID for a single-line row.
        String ssid = e.ssid.length() ? e.ssid : String("<hidden>");
        if (ssid.length() > 20) { ssid = ssid.substring(0, 20); ssid += "..."; }
        tft.print(ssid);

        tft.setTextFont(1);
        tft.setTextColor(sel ? TFT_YELLOW : p.textDim, bg);
        tft.setCursor(4, y + 14);
        tft.printf("ch %2u  %s  %d dBm", e.channel, encName(e.encType), e.rssi);
        tft.setTextFont(2);
    }

    theme::drawFooter(tft, "SEL = rescan   LEFT = back");

    lastResults_ = n;
}

} // namespace ui
