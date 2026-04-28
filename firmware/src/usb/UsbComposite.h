#pragma once

namespace usb {

// Bring up the USB composite stack once all desired interfaces (MSC, HID,
// vendor, etc.) have been registered. Called from main::setup() after
// board::startStorageTask so the SD card has a chance to mount first.
void start();

// True once USB has been configured by the host (enumeration succeeded).
bool connected();

} // namespace usb
