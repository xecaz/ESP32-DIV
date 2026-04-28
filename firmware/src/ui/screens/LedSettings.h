#pragma once

#include <Arduino.h>
#include "../Screen.h"

namespace ui {

// Scrollable settings page for the 4 front WS2812s. Rows:
//   [0] Enable toggle
//   [1] Pin (tap to edit)
//   [2] Brightness (0-255)
//   [3] Test button (plays the RX/TX colours for each channel)
//   [4..11] per-channel RX/TX colours (tap each to type a hex)
class LedSettingsScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    int scrollTop_ = 0;
    int cursor_    = 0;

    void activate(int row);
};

} // namespace ui
