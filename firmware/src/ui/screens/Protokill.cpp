#include "Protokill.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/Nrf24Driver.h"

namespace ui {

void ProtokillScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Protokill");

    armed_ = false;
    dirty();
}

void ProtokillScreen::onExit(TFT_eSPI&) {
    radio::nrf24::stopJammer();
}

bool ProtokillScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    switch (e.key) {
        case Key::Left: pop(); return true;
        case Key::Select: case Key::Right:
            armed_ = !armed_;
            if (armed_) radio::nrf24::startJammer(radio::nrf24::JamBand::Protokill);
            else        radio::nrf24::stopJammer();
            startedMs_  = millis();
            lastSecond_ = 0;
            dirty();
            return true;
        default: return false;
    }
}

void ProtokillScreen::onTick(uint32_t nowMs) {
    if (!armed_) return;
    uint32_t sec = (nowMs - startedMs_) / 1000;
    if (sec != lastSecond_) {
        lastSecond_ = sec;
        dirty();
    }
}

void ProtokillScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);

    tft.setTextFont(4);
    tft.setTextColor(armed_ ? p.warn : p.textDim, p.bg);
    tft.setCursor(30, 60);
    tft.print(armed_ ? "JAMMING" : "IDLE");

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 110);
    tft.print("Wideband 2.4 GHz jam");
    tft.setCursor(8, 128);
    tft.print("WiFi + BLE + Zigbee");
    tft.setCursor(8, 146);
    tft.print("~30 channels concurrent");

    tft.setTextColor(TFT_ORANGE, p.bg);
    tft.setCursor(8, 180);
    tft.print("!! illegal in most");
    tft.setCursor(8, 198);
    tft.print("   jurisdictions.");
    tft.setCursor(8, 216);
    tft.print("   bench/lab use only");

    if (armed_) {
        tft.setTextColor(TFT_YELLOW, p.bg);
        tft.setCursor(8, 250);
        uint32_t elapsed = millis() - startedMs_;
        tft.printf("active for %lus", (unsigned long)(elapsed / 1000));
    }

    theme::drawFooter(tft, "SEL = arm/disarm   LEFT = back");
}

} // namespace ui
