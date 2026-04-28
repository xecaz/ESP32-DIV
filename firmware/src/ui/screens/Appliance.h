#pragma once

#include "../Screen.h"

namespace ui {

// HackRF-style "Appliance" boot screen. Shown directly when
// storage::usbMode == Appliance. The TFT becomes a static status card —
// no menus, no touch dispatch — and the device is fully driven by the
// host over USB.
//
// Escape gesture: hold SELECT for ≥3 s to flip NVS back to Standalone
// and reboot. This is the *only* way out, by design — matching HackRF's
// dedicated-appliance feel and avoiding mode-switch bugs mid-operation.
class ApplianceScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    uint32_t bootMs_         = 0;
    uint32_t selectDownMs_   = 0;   // 0 = SELECT not held
    uint32_t lastRefreshMs_  = 0;
    bool     warnedHostMs_   = false;

    void rebootToStandalone();
};

} // namespace ui
