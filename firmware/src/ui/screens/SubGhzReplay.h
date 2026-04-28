#pragma once

#include <Arduino.h>
#include "../Screen.h"

namespace ui {

// Record + save. SEL on a fresh capture opens the keyboard to name it.
class SubGhzReplayScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint32_t freqHz_ = 433920000;
    bool     lastHadCapture_ = false;
    String   lastSave_;
    uint32_t saveMs_ = 0;
};

// Browse /subghz/*.bin, load + transmit on SELECT.
class SubGhzProfilesScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;
private:
    static constexpr int MAX_FILES = 32;
    String   files_[MAX_FILES];
    int      count_     = 0;
    int      cursor_    = 0;
    int      scrollTop_ = 0;
    uint32_t lastTxMs_  = 0;
    int      txCount_   = 0;
    void reload();
};

class SubGhzJammerScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint32_t freqHz_ = 433920000;
    bool     armed_  = false;
};

} // namespace ui
