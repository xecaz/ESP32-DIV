#pragma once

// USB-CDC console for the OTG composite.
//
// This board has no UART bridge (the CP2102 was dropped this revision), so the
// Arduino `Serial` (UART0) goes nowhere. Instead we add a CDC ACM interface to
// the TinyUSB composite (alongside MSC + HID) and route all logging to it, so a
// serial console comes back over the same USB-C cable — and esptool can auto-
// reset into the bootloader via CDC DTR/RTS (no more BOOT+RESET dance).
//
// `USBSerial` is a real `USBCDC`; writes are dropped instantly (never block)
// while no host is connected, so logging before enumeration is free. The CDC
// interface auto-registers in the USBCDC constructor (static init), so it joins
// the composite descriptor before USB.begin(); usb::start() just calls begin()
// to wire up the RX queue and reboot handler.
#include <USBCDC.h>

extern USBCDC USBSerial;
