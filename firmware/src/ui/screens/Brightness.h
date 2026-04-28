#pragma once

#include "../Screen.h"

namespace ui {

// Simple brightness slider: UP/DOWN or +/- buttons step the LEDC level,
// SELECT saves, LEFT backs out. Applies live so the user can eyeball it.
class BrightnessScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint8_t initial_ = 200;
    uint8_t value_   = 200;

    void apply();
    void saveAndExit();
};

} // namespace ui
