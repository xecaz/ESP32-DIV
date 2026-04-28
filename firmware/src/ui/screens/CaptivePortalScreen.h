#pragma once

#include <Arduino.h>
#include "../Screen.h"

namespace ui {

class CaptivePortalScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    String ssid_       = "Free WiFi";
    int    lastSubs_   = 0;
    int    lastClients_ = 0;
    int    scrollTop_  = 0;      // index of the top visible submission
    int    cursor_     = 0;      // highlighted submission

    void restartPortal();
};

} // namespace ui
