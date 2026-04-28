#pragma once

#include <Arduino.h>

namespace usb {
namespace hid {

// USB HID keyboard. Call begin() once after USB.begin() — the class
// registers itself with TinyUSB. Then type() sends any ASCII string as a
// keystroke burst with the given inter-key delay.
bool begin();
bool ready();
void type(const String& s, uint16_t interKeyMs = 10);

// Send a single key combo, e.g. ctrlAltDel() = modifiers 0x05 with key 0x4C.
void sendChord(uint8_t modifiers, uint8_t key);

} // namespace hid
} // namespace usb
