#pragma once

#include "../Screen.h"

namespace ui {

class PacketMonitorScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint32_t lastPkts_  = 0;
    uint32_t lastBytes_ = 0;
    uint32_t startedMs_ = 0;

    // Splits the render into "draw once" (labels, borders, filename) and
    // "refresh" (numbers, graph) to avoid the whole-screen flicker from
    // clearing the entire body every tick.
    bool     staticDrawn_ = false;
    uint16_t lastRate_[60] = {};
    uint16_t lastPeak_  = 0;
};

} // namespace ui
