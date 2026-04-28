#include "BleSpoofer.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/BleAdvertiser.h"

namespace ui {

void BleSpooferScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "BLE Spoofer");

    startedMs_ = millis();
    radio::ble_adv::start(radio::ble_adv::Mode::SpoofApple, 0);
    dirty();
}

void BleSpooferScreen::onExit(TFT_eSPI&) {
    radio::ble_adv::stop();
}

bool BleSpooferScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    switch (e.key) {
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Up:
            radio::ble_adv::spoofPrev();
            // re-apply by nudging the advertiser with the new index.
            radio::ble_adv::start(radio::ble_adv::Mode::SpoofApple,
                                  radio::ble_adv::currentSpoofIndex());
            dirty();
            return true;
        case Key::Down:
            radio::ble_adv::spoofNext();
            radio::ble_adv::start(radio::ble_adv::Mode::SpoofApple,
                                  radio::ble_adv::currentSpoofIndex());
            dirty();
            return true;
        case Key::Select: case Key::Right:
            // Cycle + redraw
            radio::ble_adv::spoofNext();
            radio::ble_adv::start(radio::ble_adv::Mode::SpoofApple,
                                  radio::ble_adv::currentSpoofIndex());
            dirty();
            return true;
        default: return false;
    }
}

void BleSpooferScreen::onTick(uint32_t) {
    uint32_t tx = radio::ble_adv::txCount();
    if (tx != lastTx_) { lastTx_ = tx; dirty(); }
}

void BleSpooferScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    uint8_t idx = radio::ble_adv::currentSpoofIndex();

    // Hero label — current spoofed device.
    tft.fillRect(0, 40, 240, 60, p.bg);
    tft.setTextFont(4);
    tft.setTextColor(TFT_YELLOW, p.bg);
    const char* name = radio::ble_adv::spoofName(idx);
    int w = tft.textWidth(name);
    tft.setCursor((240 - w) / 2, 56);
    tft.print(name);

    // Indices
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 110);
    tft.printf("preset %d / 17", idx + 1);
    tft.setCursor(8, 128);
    tft.printf("tx frames: %lu", (unsigned long)radio::ble_adv::txCount());

    uint32_t elapsed = millis() - startedMs_;
    tft.setCursor(8, 146);
    tft.printf("running: %lus", (unsigned long)(elapsed / 1000));

    theme::drawFooter(tft, "UP/DOWN = preset   LEFT = back");
}

} // namespace ui
