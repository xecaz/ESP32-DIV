#include "SourApple.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/BleAdvertiser.h"

namespace ui {

void SourAppleScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Sour Apple");

    startedMs_ = millis();
    radio::ble_adv::start(radio::ble_adv::Mode::SourApple);
    dirty();
}

void SourAppleScreen::onExit(TFT_eSPI&) {
    radio::ble_adv::stop();
}

bool SourAppleScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;
    if (e.key == Key::Left && e.type == EventType::KeyDown) { pop(); return true; }
    return false;
}

void SourAppleScreen::onTick(uint32_t) {
    uint32_t tx = radio::ble_adv::txCount();
    if (tx != lastTx_) { lastTx_ = tx; dirty(); }
}

void SourAppleScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 200, p.bg);

    tft.setTextFont(4);
    tft.setTextColor(p.warn, p.bg);
    tft.setCursor(40, 60);
    tft.print("ACTIVE");

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 110);
    tft.print("Spoofing malformed");
    tft.setCursor(8, 128);
    tft.print("Apple BLE adv packets.");

    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 160);
    tft.print("Defensive research only.");
    tft.setCursor(8, 178);
    tft.print("Do not target devices you");
    tft.setCursor(8, 196);
    tft.print("don't own.");

    tft.setTextColor(TFT_YELLOW, p.bg);
    tft.setCursor(8, 230);
    tft.printf("tx: %lu", (unsigned long)radio::ble_adv::txCount());
    tft.setCursor(8, 250);
    uint32_t elapsed = millis() - startedMs_;
    tft.printf("up: %lus", (unsigned long)(elapsed / 1000));

    theme::drawFooter(tft, "LEFT = back");
}

} // namespace ui
