#pragma once

#include "../Screen.h"

namespace ui {

class BleScanScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    int      cursor_   = 0;
    int      scrollTop_ = 0;
    uint32_t lastRenderCount_ = 0;
    uint32_t startedMs_ = 0;
};

} // namespace ui
