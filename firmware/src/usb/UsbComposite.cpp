#include "UsbComposite.h"

#include <Arduino.h>
#include <USB.h>

#include "UsbMsc.h"
#include "UsbHid.h"
#include "UsbHostWatch.h"
#include "UsbSerial.h"
#include "../hw/Board.h"
#include "../storage/Settings.h"

// The composite's CDC console. Defined here (single TU); declared extern in
// UsbSerial.h for every logging site. Constructing it registers the CDC
// interface into the TinyUSB descriptor at static-init time, before USB.begin().
USBCDC USBSerial(0);

namespace usb {

namespace {
bool g_started = false;
}

void start() {
    if (g_started) return;
    g_started = true;

    USB.VID(0x303A);       // Espressif
    USB.PID(0x4002);       // arbitrary dev/testing PID in Espressif's block
    USB.productName("CTRL//VOID");
    USB.manufacturerName("cifertech + rebuild");
    USB.serialNumber("ESP32DIV-0001");

    // Register interfaces before USB.begin(). Order matters: the composite
    // descriptor is locked in when USB.begin() is called.
    //   CDC #0 — console + esptool auto-reset (manual USBCDC, NOT CDC_ON_BOOT;
    //            CDC_ON_BOOT would call USB.begin() before setup() and lock the
    //            descriptor before HID/MSC register — see CLAUDE.md).
    //   HID    — keyboard (wired Ducky foundation)
    //   MSC    — deferred (see UsbMsc.cpp)
    USBSerial.begin();
    hid::begin();

    USB.begin();
    watchHostEvents();  // after USB.begin() so events are delivered
    USBSerial.println("[usb] composite started (CDC + HID; MSC pending)");
}

bool connected() {
    // Reasonable proxy: once USB has been started and the VBUS is alive.
    return g_started;
}

} // namespace usb
