#pragma once

#include "../Screen.h"

namespace ui {

// Theme selector. Persists to storage::Settings::theme. A full repaint
// across every screen on theme change is deferred — at the moment the
// selection is read at boot and applied by colour-aware screens (main
// menu, about), and every other screen will pick up the new palette the
// next time its onEnter fires.
class ThemePickScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint8_t selected_ = 0;
};

} // namespace ui
