#include "UsbHid.h"

#include <USB.h>
#include <USBHIDKeyboard.h>

namespace usb {
namespace hid {

namespace {
USBHIDKeyboard g_kb;
bool           g_started = false;
}

bool begin() {
    if (g_started) return true;
    g_kb.begin();   // registers the HID interface; must be before USB.begin()
    g_started = true;
    return true;
}

bool ready() {
    // USBHIDKeyboard doesn't expose an "is host connected" predicate, but
    // writes after enumeration silently succeed, so we just return whether
    // we've called begin().
    return g_started;
}

void type(const String& s, uint16_t interKeyMs) {
    if (!g_started) return;
    for (size_t i = 0; i < s.length(); ++i) {
        g_kb.write(s[i]);
        if (interKeyMs) delay(interKeyMs);
    }
}

void sendChord(uint8_t modifiers, uint8_t key) {
    if (!g_started) return;
    // USBHIDKeyboard doesn't take raw HID codes; bodge by pressing named
    // keys instead for the common case. More robust chord support lives
    // in the dedicated DuckyScript runner (M10).
    if (modifiers) g_kb.press(modifiers);
    if (key)       g_kb.press(key);
    delay(10);
    g_kb.releaseAll();
}

} // namespace hid
} // namespace usb
