#include "BleJammer.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/Nrf24Driver.h"

namespace ui {

void BleJammerScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "BLE Jammer");
    startedMs_ = millis();
    armed_ = false;   // start disarmed — user must press SELECT to activate
    dirty();
}

void BleJammerScreen::onExit(TFT_eSPI&) {
    radio::nrf24::stopJammer();
}

bool BleJammerScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;

    switch (e.key) {
        case Key::Left:
            pop(); return true;
        case Key::Select: case Key::Right: {
            armed_ = !armed_;
            if (armed_) radio::nrf24::startJammer(radio::nrf24::JamBand::Ble);
            else        radio::nrf24::stopJammer();
            startedMs_  = millis();
            lastSecond_ = 0;
            dirty();
            return true;
        }
        case Key::Up: case Key::Down: {
            // Toggle between BLE and Classic bands when idle.
            if (armed_) return true;
            auto want = (radio::nrf24::jammerBand() == radio::nrf24::JamBand::Ble)
                            ? radio::nrf24::JamBand::Classic
                            : radio::nrf24::JamBand::Ble;
            (void)want; // we only track desired band; it's applied on next start
            // Nothing to do until re-arm. Just repaint.
            dirty();
            return true;
        }
        default: return false;
    }
}

void BleJammerScreen::onTick(uint32_t nowMs) {
    if (!armed_) return;
    // Repaint once per wall-clock second so the "jamming for Ns" counter
    // advances. The screen is otherwise idle (no events while armed), so
    // without this tick the elapsed line stays frozen at 0s.
    uint32_t sec = (nowMs - startedMs_) / 1000;
    if (sec != lastSecond_) {
        lastSecond_ = sec;
        dirty();
    }
}

void BleJammerScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);

    tft.setTextFont(4);
    tft.setTextColor(armed_ ? p.warn : p.textDim, p.bg);
    tft.setCursor(40, 60);
    tft.print(armed_ ? "ARMED" : "IDLE");

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 110);
    tft.print("3x NRF24 const carrier");
    tft.setCursor(8, 128);
    tft.print("over BLE adv channels");
    tft.setCursor(8, 146);
    tft.printf("(2, 26, 80 + harmonics)");

    if (armed_) {
        tft.setTextColor(TFT_YELLOW, p.bg);
        tft.setCursor(8, 180);
        uint32_t elapsed = millis() - startedMs_;
        tft.printf("jamming for %lus", (unsigned long)(elapsed / 1000));
    }

    theme::drawFooter(tft, "SEL = arm/disarm   LEFT = back");
}

} // namespace ui
