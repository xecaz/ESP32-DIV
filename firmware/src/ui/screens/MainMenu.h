#pragma once

#include "../Screen.h"

namespace ui {

class MainMenu : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    int cursor_ = 0;
    int lastCursor_ = -1;
};

} // namespace ui
