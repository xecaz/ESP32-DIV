#include "DuckyUsb.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../hw/Board.h"
#include "../../usb/DuckyRunner.h"
#include "../../usb/UsbHostWatch.h"

namespace ui {

namespace {
constexpr int ROW_H = 22;
constexpr int LIST_Y = 98;
constexpr int VISIBLE = 7;
}

void DuckyUsbScreen::reload() {
    count_ = usb::ducky::listScripts(files_, MAX_FILES);
    if (cursor_ >= count_) cursor_ = count_ ? count_ - 1 : 0;
}

void DuckyUsbScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Ducky (USB)");
    reload();
    dirty();
}

void DuckyUsbScreen::onExit(TFT_eSPI&) {
    // Leave the arm alone — user might navigate away and come back. The
    // runner fires on host-connect regardless of screen state.
}

bool DuckyUsbScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    switch (e.key) {
        case Key::Left:
            if (e.type == EventType::KeyDown) {
                usb::ducky::disarm();
                pop();
            }
            return true;
        case Key::Up:
            if (!count_) return true;
            cursor_ = (cursor_ - 1 + count_) % count_;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
            dirty();
            return true;
        case Key::Down:
            if (!count_) return true;
            cursor_ = (cursor_ + 1) % count_;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
            dirty();
            return true;
        case Key::Select:
            if (e.type == EventType::KeyDown && count_) {
                usb::ducky::arm(files_[cursor_]);
                dirty();
            }
            return true;
        case Key::Right:
            // RIGHT = run immediately (don't wait for re-enumeration).
            if (e.type == EventType::KeyDown && count_) {
                usb::ducky::arm(files_[cursor_]);
                usb::ducky::runNow();
                dirty();
            }
            return true;
        default: return false;
    }
}

void DuckyUsbScreen::onTick(uint32_t nowMs) {
    // Fire if armed + host just (re)connected.
    usb::ducky::maybeRun();
    // Repaint at 4 Hz while armed so the status line feels live.
    static uint32_t lastLazy = 0;
    if (usb::ducky::armed() && nowMs - lastLazy > 250) {
        lastLazy = nowMs;
        dirty();
    }
}

void DuckyUsbScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 30, 240, 290, p.bg);
    tft.setTextFont(2);

    // Host connection status (SD state is conveyed by the bottom warning
    // strip when absent — no "SD OK" chrome when everything is fine).
    tft.setTextColor(usb::hostConnected() ? p.ok : p.textDim, p.bg);
    tft.setCursor(8, 34);
    tft.print(usb::hostConnected() ? "HOST: up" : "HOST: -");

    // Arm banner.
    if (usb::ducky::armed()) {
        tft.fillRect(0, 54, 240, 36, 0x2001);  // dark red
        tft.setTextColor(TFT_WHITE, 0x2001);
        tft.setCursor(8, 56);
        tft.printf("ARMED: %s", usb::ducky::armedName().c_str());
        tft.setTextColor(TFT_YELLOW, 0x2001);
        tft.setTextFont(1);
        tft.setCursor(8, 72);
        tft.print(usb::hostConnected() ? "running on next host connect..."
                                       : "plug USB into target to fire");
        tft.setTextFont(2);
    } else {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 60);
        tft.print("SEL = arm   RIGHT = run now");
    }

    // File list.
    if (count_ == 0) {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, LIST_Y + 12);
        tft.print("no scripts in /rubberducky/");
        tft.setTextFont(1);
        tft.setCursor(8, LIST_Y + 34);
        tft.print("drop .txt files on the SD card");
        tft.setTextFont(2);
    } else {
        for (int vi = 0; vi < VISIBLE; ++vi) {
            int i = scrollTop_ + vi;
            if (i >= count_) break;
            int y = LIST_Y + vi * ROW_H;
            bool sel = (i == cursor_);
            uint16_t bg = sel ? p.selBg : p.bg;
            uint16_t fg = sel ? p.selFg : p.textDim;
            tft.fillRect(0, y, 240, ROW_H - 2, bg);
            tft.setTextColor(fg, bg);
            tft.setCursor(8, y + 2);
            String name = files_[i];
            if (name.length() > 26) name = name.substring(0, 26) + "…";
            tft.print(name);
        }
    }

    // Last-run summary. Moved above y=282 so the SD-warning strip (when
    // shown) doesn't cover it.
    if (usb::ducky::lastRunMs()) {
        uint32_t ago = (millis() - usb::ducky::lastRunMs()) / 1000;
        tft.setTextFont(1);
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 266);
        tft.printf("last run: %lus ago  (%d lines)",
                   (unsigned long)ago, usb::ducky::lastRunLines());
        tft.setTextFont(2);
    }

    theme::drawSdWarningIfMissing(tft);
    theme::drawFooter(tft, "SEL=arm  RIGHT=run  LEFT=disarm");
}

} // namespace ui
