#pragma once

#include <Arduino.h>
#include "../Screen.h"

namespace ui {

// Browse /rubberducky/*.txt, arm one, show armed state + countdown. When
// the USB host connects (enumeration completes), the runner auto-fires
// the script. LEFT backs out and disarms.
class DuckyUsbScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    static constexpr int MAX_FILES = 32;
    String  files_[MAX_FILES];
    int     count_     = 0;
    int     cursor_    = 0;
    int     scrollTop_ = 0;
    void    reload();
};

} // namespace ui
