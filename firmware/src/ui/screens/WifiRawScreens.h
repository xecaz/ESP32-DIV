#pragma once

#include "../Screen.h"
#include "../../radio/WifiRaw.h"

namespace ui {

// One shared screen class for the three simple WifiRaw features — deauth
// detector, beacon spammer, deauth attacker. Each instance is constructed
// with the mode it should drive.
class WifiRawScreen : public Screen {
public:
    explicit WifiRawScreen(radio::wifi_raw::Mode mode, const char* title);

    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    radio::wifi_raw::Mode mode_;
    const char*           title_;
    uint32_t              startedMs_ = 0;
    uint32_t              lastSnapshotTx_ = 0, lastSnapshotRx_ = 0, lastSnapshotDa_ = 0;
};

} // namespace ui
