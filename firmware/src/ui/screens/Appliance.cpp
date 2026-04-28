#include "Appliance.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_system.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../hw/Board.h"
#include "../../storage/Settings.h"
#include "../../usb/UsbHostWatch.h"

namespace ui {

namespace {
constexpr uint32_t HOLD_TO_EXIT_MS = 3000;
}

void ApplianceScreen::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);

    // Static "APPLIANCE MODE" header in red so it's obvious this isn't
    // the regular UI — no mistaking it for a frozen menu.
    tft.fillRect(0, 0, 240, 40, p.warn);
    tft.setTextFont(4);
    tft.setTextColor(p.selFg, p.warn);
    tft.setCursor(8, 8);
    tft.print("APPLIANCE");

    bootMs_         = millis();
    selectDownMs_   = 0;
    lastRefreshMs_  = 0;
    dirty();
}

void ApplianceScreen::onExit(TFT_eSPI&) {}

void ApplianceScreen::rebootToStandalone() {
    auto& s = storage::mut();
    s.usbMode = storage::UsbMode::Standalone;
    storage::save();
    Serial.println("[appliance] SELECT held 3s — rebooting to Standalone");
    delay(50);
    esp_restart();
}

bool ApplianceScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    // The escape gesture is the only honored input. Track SELECT-down
    // timestamp; the 3-second wait is checked from onTick (so we don't
    // need a key-up to react). Other keys are ignored entirely.
    if (e.key != Key::Select) return true;  // swallow

    if (e.type == EventType::KeyDown) {
        selectDownMs_ = e.tMs ? e.tMs : millis();
        dirty();
    } else if (e.type == EventType::KeyUp) {
        selectDownMs_ = 0;
        dirty();
    }
    return true;
}

void ApplianceScreen::onTick(uint32_t nowMs) {
    if (selectDownMs_ && (nowMs - selectDownMs_) >= HOLD_TO_EXIT_MS) {
        rebootToStandalone();
        return;
    }
    // Refresh the status card 2× per second so up-time + USB state +
    // hold-progress bar stay live.
    if (nowMs - lastRefreshMs_ >= 500) {
        lastRefreshMs_ = nowMs;
        dirty();
    }
}

void ApplianceScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();

    // Status block — just below the red header, refreshed in place.
    tft.fillRect(0, 44, 240, 230, p.bg);
    tft.setTextFont(2);

    int y = 52;
    auto label = [&](const char* k) {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, y);
        tft.print(k);
    };
    auto valueStr = [&](uint16_t color, const String& s) {
        tft.setTextColor(color, p.bg);
        tft.setCursor(96, y);
        tft.print(s);
        y += 18;
    };

    label("USB host:");
    valueStr(usb::hostConnected() ? p.ok : p.textDim,
             usb::hostConnected() ? String("connected") : String("waiting"));

    label("SD card:");
    valueStr(board::sdMounted() ? p.ok : p.warn,
             board::sdMounted() ? String("mounted") : String("absent"));

    uint8_t mac[6]{};
    WiFi.macAddress(mac);
    label("WiFi MAC:");
    char macBuf[20];
    snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    valueStr(p.text, String(macBuf));

    uint32_t up = (millis() - bootMs_) / 1000;
    label("up:");
    char upBuf[16];
    snprintf(upBuf, sizeof(upBuf), "%lus", (unsigned long)up);
    valueStr(p.text, String(upBuf));

    // Hint block.
    y += 8;
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, y);
    tft.print("Host-driven mode. The TFT is");
    y += 16;
    tft.setCursor(8, y);
    tft.print("a status card — radios are");
    y += 16;
    tft.setCursor(8, y);
    tft.print("driven over USB CDC/HID/MSC.");
    y += 24;

    tft.setTextColor(p.warn, p.bg);
    tft.setCursor(8, y);
    tft.print("Hold SELECT 3s = back to");
    y += 16;
    tft.setCursor(8, y);
    tft.print("Standalone (reboots).");

    // Progress bar for the SELECT hold so the user knows the gesture is
    // registering. Sits at the bottom, above the footer.
    const int BAR_Y = 280;
    const int BAR_H = 14;
    tft.drawRect(8, BAR_Y, 224, BAR_H, p.textDim);
    if (selectDownMs_) {
        uint32_t held = millis() - selectDownMs_;
        if (held > HOLD_TO_EXIT_MS) held = HOLD_TO_EXIT_MS;
        int w = (int)((held * 222) / HOLD_TO_EXIT_MS);
        tft.fillRect(9, BAR_Y + 1, w, BAR_H - 2, p.warn);
        tft.fillRect(9 + w, BAR_Y + 1, 222 - w, BAR_H - 2, p.bg);
    } else {
        tft.fillRect(9, BAR_Y + 1, 222, BAR_H - 2, p.bg);
    }

    theme::drawFooter(tft, "USB peripheral — host drives radios");
}

} // namespace ui
