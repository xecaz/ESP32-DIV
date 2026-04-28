#pragma once

#include "../Screen.h"

namespace ui {

// Live counters of PCF8574 I²C poller health. Lets the user A/B test
// hardware mods (pull-up swaps, bus reroutes) by watching the OK/FAIL
// rate change in real time. SELECT zeroes the counters; LEFT exits.
class I2cHealthScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint32_t lastUpdateMs_ = 0;
    uint32_t prevOk_       = 0;
    uint32_t prevFail_     = 0;
    uint32_t okPerSec_     = 0;
    uint32_t failPerSec_   = 0;
};

} // namespace ui
