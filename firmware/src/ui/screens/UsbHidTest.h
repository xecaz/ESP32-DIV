#pragma once

#include "../Screen.h"

namespace ui {

// Plug the device into a host, focus a text field, and every SEL will
// type a short test string. Proves the USB HID composite interface is
// up and the host enumerated it as a keyboard.
class UsbHidTestScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;
private:
    int typedCount_ = 0;
};

} // namespace ui
