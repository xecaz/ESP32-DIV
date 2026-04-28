#pragma once

#include <Arduino.h>   // String
#include "../Screen.h"

namespace ui {

class IrRecordScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    bool     lastHad_  = false;
    String   lastSave_;  // last filename saved this session
    uint32_t saveMs_   = 0;
};

// IR Replay is now the Saved-Profiles browser — every replay reads from SD,
// so a separate "replay last transient capture" screen is redundant.
class IrProfilesScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;
private:
    static constexpr int MAX_FILES = 32;
    String   files_[MAX_FILES];
    int      count_    = 0;
    int      cursor_   = 0;
    int      scrollTop_= 0;
    uint32_t lastTxMs_ = 0;
    int      txCount_  = 0;

    void reload();
};

} // namespace ui
