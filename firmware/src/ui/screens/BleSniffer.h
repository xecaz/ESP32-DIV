#pragma once

#include "../Screen.h"

namespace ui {

// The sniffer shares the BLE scan driver with the scanner, but its UI
// displays per-device detail: full MAC, RSSI trend, re-advertisement count,
// and flags "suspicious" entries (MAC that changes often = randomized).
class BleSnifferScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    int cursor_ = 0;
    int scrollTop_ = 0;
    uint32_t startedMs_ = 0;
    int lastCount_ = 0;
};

} // namespace ui
