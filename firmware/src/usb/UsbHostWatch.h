#pragma once

#include <stdint.h>

namespace usb {

// Register a USB event listener so we know when the host enumerates /
// suspends us. Must be called after USB.begin(). Safe to call once.
void watchHostEvents();

// True when the host has an active non-suspended USB connection.
bool hostConnected();

// Time (millis) of the last transition to hostConnected=true. 0 = never.
uint32_t hostConnectedAtMs();

} // namespace usb
