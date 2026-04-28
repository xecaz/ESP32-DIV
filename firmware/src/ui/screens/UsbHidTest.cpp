#include "UsbHidTest.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../usb/UsbHid.h"

namespace ui {

void UsbHidTestScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "USB HID Test");
    dirty();
}

bool UsbHidTestScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    switch (e.key) {
        case Key::Left: pop(); return true;
        case Key::Select: case Key::Right:
            usb::hid::type("CTRL//VOID USB keyboard OK\n");
            ++typedCount_;
            dirty();
            return true;
        default: return false;
    }
}

void UsbHidTestScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 260, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 48);
    tft.print("1. plug USB-OTG port");
    tft.setCursor(8, 68);
    tft.print("   into host");
    tft.setCursor(8, 88);
    tft.print("2. focus a text field");
    tft.setCursor(8, 108);
    tft.print("3. press SEL to type");

    tft.setTextColor(TFT_YELLOW, p.bg);
    tft.setCursor(8, 150);
    tft.printf("typed %d times", typedCount_);

    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 180);
    tft.print("string: \"CTRL//VOID USB");
    tft.setCursor(8, 198);
    tft.print("         keyboard OK\\n\"");

    theme::drawFooter(tft, "SEL=type  LEFT=back");
}

} // namespace ui
