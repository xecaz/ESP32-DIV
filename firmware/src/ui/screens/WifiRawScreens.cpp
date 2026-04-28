#include "WifiRawScreens.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"

namespace ui {

WifiRawScreen::WifiRawScreen(radio::wifi_raw::Mode m, const char* title)
    : mode_(m), title_(title) {}

void WifiRawScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, title_);

    startedMs_ = millis();
    radio::wifi_raw::start(mode_);
    dirty();
}

void WifiRawScreen::onExit(TFT_eSPI&) {
    radio::wifi_raw::stop();
}

bool WifiRawScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    if (e.key == Key::Left) { pop(); return true; }
    return false;
}

void WifiRawScreen::onTick(uint32_t) {
    uint32_t tx = radio::wifi_raw::framesTxd();
    uint32_t rx = radio::wifi_raw::framesRxd();
    uint32_t da = radio::wifi_raw::deauthsRxd();
    if (tx != lastSnapshotTx_ || rx != lastSnapshotRx_ || da != lastSnapshotDa_) {
        lastSnapshotTx_ = tx; lastSnapshotRx_ = rx; lastSnapshotDa_ = da;
        dirty();
    }
}

void WifiRawScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);

    tft.setTextFont(4);
    tft.setTextColor(mode_ == radio::wifi_raw::Mode::DeauthDetect ? p.accent : p.warn,
                     p.bg);
    tft.setCursor(40, 60);
    tft.print(mode_ == radio::wifi_raw::Mode::DeauthDetect ? "LISTEN" : "ACTIVE");

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);

    uint32_t elapsed = millis() - startedMs_;
    int y = 110;

    switch (mode_) {
        case radio::wifi_raw::Mode::DeauthDetect:
            tft.setCursor(8, y);  y += 18;
            tft.printf("frames seen: %lu",
                       (unsigned long)radio::wifi_raw::framesRxd());
            tft.setCursor(8, y);  y += 18;
            tft.setTextColor(TFT_YELLOW, p.bg);
            tft.printf("deauth / disassoc: %lu",
                       (unsigned long)radio::wifi_raw::deauthsRxd());
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, y);  y += 18;
            tft.printf("hopping ch 1-11");
            break;
        case radio::wifi_raw::Mode::BeaconSpam:
            tft.setCursor(8, y);  y += 18;
            tft.printf("tx beacons: %lu",
                       (unsigned long)radio::wifi_raw::framesTxd());
            tft.setCursor(8, y);  y += 18;
            tft.print("random SSID + src MAC");
            tft.setCursor(8, y);  y += 18;
            tft.print("channels: random 1-11");
            break;
        case radio::wifi_raw::Mode::DeauthAttack:
            tft.setCursor(8, y);  y += 18;
            tft.printf("tx deauths: %lu",
                       (unsigned long)radio::wifi_raw::framesTxd());
            tft.setCursor(8, y);  y += 18;
            tft.setTextColor(TFT_ORANGE, p.bg);
            tft.print("broadcast target");
            tft.setCursor(8, y);  y += 18;
            tft.print("lab / own AP only!");
            break;
        default: break;
    }

    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 210);
    tft.printf("up: %lus", (unsigned long)(elapsed / 1000));

    theme::drawFooter(tft, "LEFT = back");
}

} // namespace ui
