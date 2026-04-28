#include "DeauthFlow.h"

#include <TFT_eSPI.h>
#include <string.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/WifiDriver.h"
#include "../../radio/WifiDeauth.h"

namespace ui {

namespace {
constexpr int ROW_H  = 28;
constexpr int LIST_Y = 56;
constexpr int VISIBLE = 7;

void fmtMac(const uint8_t* m, char* out /* >=18 */) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}
}

// ── Stage 1: AP picker ─────────────────────────────────────────────────

void DeauthApScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Deauth: pick AP");
    cursor_ = scrollTop_ = 0;
    lastCount_ = -1;
    radio::wifi::startScan();
    dirty();
}

void DeauthApScreen::onExit(TFT_eSPI&) {
    radio::wifi::stop();
}

bool DeauthApScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    int n = radio::wifi::resultCount();
    switch (e.key) {
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Up:
            if (n) { cursor_ = (cursor_ - 1 + n) % n;
                     if (cursor_ < scrollTop_) scrollTop_ = cursor_;
                     if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
                     dirty(); }
            return true;
        case Key::Down:
            if (n) { cursor_ = (cursor_ + 1) % n;
                     if (cursor_ < scrollTop_) scrollTop_ = cursor_;
                     if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
                     dirty(); }
            return true;
        case Key::Select: case Key::Right: {
            if (!n) return true;
            if (e.type != EventType::KeyDown) return true;
            radio::ScanEntry s;
            if (!radio::wifi::resultAt(cursor_, s)) return true;
            // WifiScanScreen stops its driver on exit; we must too before
            // entering the deauth listener (both own radio::Owner::Wifi).
            radio::wifi::stop();
            push(new DeauthClientScreen(s.bssid, s.channel, s.ssid.c_str()));
            return true;
        }
        default: return false;
    }
}

void DeauthApScreen::onTick(uint32_t nowMs) {
    // Drive the underlying driver state machine — scanDone() is what
    // flips "running" to "have results", and without this call the list
    // stays empty forever. The return value is edge-triggered via
    // resultCount() below so we don't re-dirty on every tick.
    radio::wifi::scanDone();

    int n = radio::wifi::resultCount();
    if (n != lastCount_) {
        lastCount_ = n;
        dirty();
    }
    // Lightly animate "scanning…" at 2 Hz while the scan is in flight.
    static uint32_t lastLazy = 0;
    if (radio::wifi::scanRunning() && nowMs - lastLazy > 500) {
        lastLazy = nowMs;
        dirty();
    }
}

void DeauthApScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    int n = radio::wifi::resultCount();

    tft.fillRect(0, 30, 240, 20, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 32);
    if (radio::wifi::scanRunning()) tft.print("scanning...");
    else                            tft.printf("%d APs", n);

    tft.fillRect(0, LIST_Y, 240, VISIBLE * ROW_H, p.bg);
    for (int vi = 0; vi < VISIBLE; ++vi) {
        int i = scrollTop_ + vi;
        if (i >= n) break;
        radio::ScanEntry s;
        if (!radio::wifi::resultAt(i, s)) continue;

        int y = LIST_Y + vi * ROW_H;
        bool sel = (i == cursor_);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.textDim;
        tft.fillRect(0, y, 240, ROW_H - 2, bg);

        tft.setTextFont(2);
        tft.setTextColor(fg, bg);
        tft.setCursor(4, y + 2);
        String ssid = s.ssid.length() ? s.ssid : String("<hidden>");
        if (ssid.length() > 20) ssid = ssid.substring(0, 20) + "…";
        tft.print(ssid);

        tft.setTextFont(1);
        tft.setTextColor(sel ? TFT_YELLOW : p.textDim, bg);
        tft.setCursor(4, y + 16);
        char macStr[18]; fmtMac(s.bssid, macStr);
        tft.printf("%s  ch%u  %ddBm", macStr, s.channel, s.rssi);
    }

    theme::drawFooter(tft, "SEL=pick  LEFT=back");
}

// ── Stage 2: client picker ─────────────────────────────────────────────

DeauthClientScreen::DeauthClientScreen(const uint8_t bssid[6], uint8_t channel,
                                       const char* ssid)
    : channel_(channel), ssid_(ssid) {
    memcpy(bssid_, bssid, 6);
}

void DeauthClientScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Pick target");

    cursor_ = 0;  // 0 = "All clients"
    scrollTop_ = 0;
    lastCount_ = -1;
    radio::wifi_deauth::startListen(bssid_, channel_);
    dirty();
}

void DeauthClientScreen::onExit(TFT_eSPI&) {
    // Only stop if we're NOT transitioning to the attack screen.
    if (!radio::wifi_deauth::attacking()) radio::wifi_deauth::stop();
}

bool DeauthClientScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    int n = radio::wifi_deauth::clientCount();
    int total = 1 + n;  // "broadcast" + clients

    switch (e.key) {
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Up:
            cursor_ = (cursor_ - 1 + total) % total;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
            dirty();
            return true;
        case Key::Down:
            cursor_ = (cursor_ + 1) % total;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
            dirty();
            return true;
        case Key::Select: case Key::Right: {
            if (e.type != EventType::KeyDown) return true;
            String label;
            if (cursor_ == 0) {
                radio::wifi_deauth::attack(nullptr);
                label = "all clients on " + ssid_;
            } else {
                radio::wifi_deauth::Client c;
                if (!radio::wifi_deauth::clientAt(cursor_ - 1, c)) return true;
                radio::wifi_deauth::attack(c.mac);
                char buf[40];
                snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                         c.mac[0], c.mac[1], c.mac[2], c.mac[3], c.mac[4], c.mac[5]);
                label = buf;
            }
            push(new DeauthActiveScreen(label));
            return true;
        }
        default: return false;
    }
}

void DeauthClientScreen::onTick(uint32_t nowMs) {
    int n = radio::wifi_deauth::clientCount();
    if (n != lastCount_) { lastCount_ = n; dirty(); }
    // Refresh the "seen/match" diagnostic line at 2 Hz.
    static uint32_t lastLazy = 0;
    if (nowMs - lastLazy > 500) { lastLazy = nowMs; dirty(); }
}

void DeauthClientScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    int n = radio::wifi_deauth::clientCount();

    // Header subtitle: SSID + BSSID
    tft.fillRect(0, 30, 240, 22, p.bg);
    tft.setTextFont(1);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 34);
    String s = ssid_.length() ? ssid_ : String("<hidden>");
    if (s.length() > 22) s = s.substring(0, 22) + "…";
    tft.printf("%s  ch%u", s.c_str(), channel_);

    tft.fillRect(0, LIST_Y, 240, VISIBLE * ROW_H, p.bg);

    // Row 0: "All clients (broadcast)"
    auto drawRow = [&](int row, int vi, bool sel, const char* line1,
                       const char* line2) {
        int y = LIST_Y + vi * ROW_H;
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.textDim;
        tft.fillRect(0, y, 240, ROW_H - 2, bg);
        tft.setTextFont(2);
        tft.setTextColor(fg, bg);
        tft.setCursor(4, y + 2);
        tft.print(line1);
        if (line2 && *line2) {
            tft.setTextFont(1);
            tft.setTextColor(sel ? TFT_YELLOW : p.textDim, bg);
            tft.setCursor(4, y + 16);
            tft.print(line2);
        }
        (void)row;
    };

    int total = 1 + n;
    for (int vi = 0; vi < VISIBLE; ++vi) {
        int i = scrollTop_ + vi;
        if (i >= total) break;
        bool sel = (i == cursor_);
        if (i == 0) {
            drawRow(i, vi, sel, "[ All clients ]", "broadcast deauth");
        } else {
            radio::wifi_deauth::Client c;
            if (!radio::wifi_deauth::clientAt(i - 1, c)) continue;
            char macStr[18]; fmtMac(c.mac, macStr);
            char sub[32];
            snprintf(sub, sizeof(sub), "%ddBm  x%lu", c.rssi,
                     (unsigned long)c.packetCount);
            drawRow(i, vi, sel, macStr, sub);
        }
    }

    if (n == 0) {
        tft.setTextFont(1);
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, LIST_Y + 2 * ROW_H);
        tft.print("discovering clients...");
        tft.setCursor(8, LIST_Y + 2 * ROW_H + 14);
        tft.print("(or pick broadcast)");
    }

    // Diagnostic line: total frames seen vs BSSID-matching frames. If
    // "seen" stays 0, the radio isn't capturing (promiscuous issue). If
    // "seen" climbs but "match" doesn't, we're on the wrong channel or
    // the BSSID is wrong. If "match" climbs but client count stays 0, we
    // only see beacons — no actual client traffic on the AP.
    tft.setTextFont(1);
    tft.setTextColor(p.textDim, p.bg);
    tft.fillRect(0, 282, 240, 18, p.bg);
    tft.setCursor(8, 284);
    tft.printf("seen:%lu  match:%lu",
               (unsigned long)radio::wifi_deauth::totalFramesSeen(),
               (unsigned long)radio::wifi_deauth::bssidMatchedFrames());

    theme::drawFooter(tft, "U/D=move  SEL=fire  LEFT=back");
}

// ── Stage 3: active attack ─────────────────────────────────────────────

DeauthActiveScreen::DeauthActiveScreen(const String& label) : label_(label) {}

void DeauthActiveScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Deauthing");
    startedMs_ = millis();
    dirty();
}

void DeauthActiveScreen::onExit(TFT_eSPI&) {
    radio::wifi_deauth::stop();
}

bool DeauthActiveScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    if (e.key == Key::Left) { pop(); return true; }
    return false;
}

void DeauthActiveScreen::onTick(uint32_t nowMs) {
    uint32_t tx = radio::wifi_deauth::txCount();
    if (tx != lastTx_) { lastTx_ = tx; dirty(); }
    // Also 2 Hz heartbeat so tx errors show up even when tx counter is 0.
    static uint32_t lastLazy = 0;
    if (nowMs - lastLazy > 500) { lastLazy = nowMs; dirty(); }
}

void DeauthActiveScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);

    tft.setTextFont(4);
    tft.setTextColor(p.warn, p.bg);
    tft.setCursor(40, 60);
    tft.print("ACTIVE");

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 110);
    tft.print("target:");
    tft.setTextColor(TFT_YELLOW, p.bg);
    String l = label_;
    if (l.length() > 28) l = l.substring(0, 28) + "…";
    tft.setCursor(8, 128);
    tft.print(l);

    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 160);
    tft.printf("ch: %u", radio::wifi_deauth::currentChannel());
    const uint8_t* b = radio::wifi_deauth::currentBssid();
    tft.setCursor(8, 178);
    tft.printf("bssid: %02X:%02X:%02X:%02X:%02X:%02X",
               b[0], b[1], b[2], b[3], b[4], b[5]);

    tft.setCursor(8, 210);
    tft.setTextColor(TFT_YELLOW, p.bg);
    tft.printf("tx frames: %lu", (unsigned long)radio::wifi_deauth::txCount());

    uint32_t errs = radio::wifi_deauth::txErrorCount();
    if (errs) {
        tft.setTextColor(p.warn, p.bg);
        tft.setCursor(8, 230);
        tft.printf("tx errors: %lu  (0x%x)",
                   (unsigned long)errs,
                   (unsigned int)radio::wifi_deauth::lastTxError());
    }

    uint32_t elapsed = millis() - startedMs_;
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 250);
    tft.printf("up: %lus", (unsigned long)(elapsed / 1000));

    theme::drawFooter(tft, "LEFT = stop");
}

} // namespace ui
