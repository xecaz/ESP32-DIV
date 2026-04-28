#pragma once

#include "../Screen.h"
#include "../../radio/Nrf24Driver.h"

namespace ui {

class NrfSpectrumScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint8_t  levels_[radio::nrf24::SPECTRUM_CHANNELS] = {};  // smoothed activity 0..255
    uint32_t lastSampleMs_ = 0;
};

} // namespace ui
