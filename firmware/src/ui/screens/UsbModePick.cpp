#include "UsbModePick.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_system.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../storage/Settings.h"

namespace ui {

namespace {
constexpr int ROW_H  = 56;
constexpr int LIST_Y = 50;

const char* MODE_NAMES[] = { "Standalone", "Bridge", "Appliance" };
const char* MODE_DESC[] = {
    "Local UI owns the device. USB exposes MSC + CDC + HID.",
    "UI keeps running plus a CDC radio-protocol channel — host can drive radios while you keep using the menu.",
    "HackRF-style. No UI, no menu. Host drives every radio over USB. Hold SELECT 3s to reboot back to Standalone.",
};

storage::UsbMode modeFor(int idx) {
    switch (idx) {
        case 0: return storage::UsbMode::Standalone;
        case 1: return storage::UsbMode::Bridge;
        case 2: return storage::UsbMode::Appliance;
    }
    return storage::UsbMode::Standalone;
}

int idxFor(storage::UsbMode m) {
    switch (m) {
        case storage::UsbMode::Standalone: return 0;
        case storage::UsbMode::Bridge:     return 1;
        case storage::UsbMode::Appliance:  return 2;
    }
    return 0;
}
} // namespace

void UsbModePickScreen::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);
    theme::drawHeader(tft, "USB Mode");
    cursor_ = idxFor(storage::get().usbMode);
    showApplianceWarn_ = false;
    dirty();
}

void UsbModePickScreen::confirmAndApply() {
    auto want = modeFor(cursor_);
    if (want == storage::get().usbMode) {
        // No-op — same mode. Just leave.
        pop();
        return;
    }
    auto& s = storage::mut();
    s.usbMode = want;
    storage::save();
    Serial.printf("[usbmode] saved %d, rebooting\n", (int)want);
    delay(50);
    esp_restart();
}

bool UsbModePickScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    if (showApplianceWarn_) {
        // Confirm-or-cancel state for Appliance — the only mode that's
        // hard to back out of (reboot required).
        if (e.type != EventType::KeyDown) return true;
        switch (e.key) {
            case Key::Select:
            case Key::Right:
                confirmAndApply();
                return true;
            case Key::Left:
                showApplianceWarn_ = false;
                dirty();
                return true;
            default: return true;
        }
    }

    switch (e.key) {
        case Key::Up:
            cursor_ = (cursor_ - 1 + 3) % 3;
            dirty();
            return true;
        case Key::Down:
            cursor_ = (cursor_ + 1) % 3;
            dirty();
            return true;
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Select:
        case Key::Right:
            if (e.type != EventType::KeyDown) return true;
            // Appliance is one-way — show the warning first.
            if (modeFor(cursor_) == storage::UsbMode::Appliance &&
                storage::get().usbMode != storage::UsbMode::Appliance) {
                showApplianceWarn_ = true;
                dirty();
                return true;
            }
            confirmAndApply();
            return true;
        default: return false;
    }
}

void UsbModePickScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();

    if (showApplianceWarn_) {
        tft.fillRect(0, 34, 240, 286, p.bg);
        tft.setTextFont(4);
        tft.setTextColor(p.warn, p.bg);
        tft.setCursor(20, 70);
        tft.print("Appliance Mode");
        tft.setTextFont(2);
        tft.setTextColor(p.text, p.bg);
        tft.setCursor(8, 120);
        tft.print("This will reboot and");
        tft.setCursor(8, 138);
        tft.print("disable the menu. The");
        tft.setCursor(8, 156);
        tft.print("only way back is to");
        tft.setCursor(8, 174);
        tft.print("hold SELECT for 3s.");
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 220);
        tft.print("SEL = confirm + reboot");
        tft.setCursor(8, 240);
        tft.print("LEFT = cancel");
        theme::drawFooter(tft, "");
        return;
    }

    tft.fillRect(0, 34, 240, 286, p.bg);

    int active = idxFor(storage::get().usbMode);
    for (int i = 0; i < 3; ++i) {
        int y = LIST_Y + i * ROW_H;
        bool sel  = (i == cursor_);
        bool live = (i == active);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.text;
        tft.fillRect(0, y, 240, ROW_H - 4, bg);

        // Radio knob: filled when this row is the *currently saved* mode.
        int knobX = 14, knobY = y + 16;
        tft.drawCircle(knobX, knobY, 7, sel ? TFT_YELLOW : p.textDim);
        if (live) {
            tft.fillCircle(knobX, knobY, 4, sel ? TFT_YELLOW : p.ok);
        }

        tft.setTextFont(4);
        tft.setTextColor(fg, bg);
        tft.setCursor(34, y + 4);
        tft.print(MODE_NAMES[i]);

        tft.setTextFont(1);
        tft.setTextColor(sel ? TFT_YELLOW : p.textDim, bg);
        // Wrap the description into the row.
        tft.setCursor(34, y + 30);
        const char* d = MODE_DESC[i];
        // Print first ~32 chars and let it wrap visually by truncating.
        char line1[40] = {};
        char line2[40] = {};
        int n = strlen(d);
        int split = 0;
        if (n > 36) {
            // Find the last space within the first 36 chars to break cleanly.
            for (int k = 36; k > 0; --k) {
                if (d[k] == ' ') { split = k; break; }
            }
            if (split == 0) split = 36;
            strncpy(line1, d, split);
            strncpy(line2, d + split + 1, sizeof(line2) - 1);
        } else {
            strncpy(line1, d, sizeof(line1) - 1);
        }
        tft.print(line1);
        if (line2[0]) {
            tft.setCursor(34, y + 42);
            tft.print(line2);
        }
        tft.setTextFont(2);
    }

    theme::drawFooter(tft, "U/D=move  SEL=apply  LEFT=back");
}

} // namespace ui
