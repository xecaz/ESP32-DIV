#pragma once

#include <stdint.h>
#include "../Screen.h"

namespace ui {

// Each About submenu entry lands on this screen with a different Page
// argument, so "Version", "MAC", "About" etc. each render a focused view
// rather than all dumping onto one dense panel.
class AboutScreen : public Screen {
public:
    enum class Page : uint8_t { Version, Mac, Credits, License };

    explicit AboutScreen(Page p = Page::Credits) : page_(p) {}

    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    Page page_;
};

} // namespace ui
