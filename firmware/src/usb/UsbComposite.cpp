#include "UsbComposite.h"

#include <Arduino.h>
#include <USB.h>

#include "UsbMsc.h"
#include "UsbHid.h"
#include "UsbHostWatch.h"
#include "../hw/Board.h"
#include "../storage/Settings.h"

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
    //   CDC #0 — console, registered automatically by ARDUINO_USB_CDC_ON_BOOT
    //   HID    — keyboard (wired Ducky foundation)
    //   MSC    — deferred (see UsbMsc.cpp)
    hid::begin();

    USB.begin();
    watchHostEvents();  // after USB.begin() so events are delivered
    Serial.println("[usb] composite started (CDC + HID; MSC pending)");
}

bool connected() {
    // Reasonable proxy: once USB has been started and the VBUS is alive.
    return g_started;
}

} // namespace usb
