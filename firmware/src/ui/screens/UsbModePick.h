#pragma once

#include "../Screen.h"

namespace ui {

// Settings screen that lets the user select which USB personality the
// device boots into:
//   Standalone → local UI owns the device, USB is MSC + CDC + HID
//   Bridge     → local UI + CDC radio-protocol open at the same time
//   Appliance  → no UI, all CPU/USB to host (HackRF-style)
//
// Selecting Appliance writes NVS and hard-reboots, since switching the
// USB descriptor set live would confuse the host.
class UsbModePickScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    int  cursor_  = 0;
    bool showApplianceWarn_ = false;
    void confirmAndApply();
};

} // namespace ui
