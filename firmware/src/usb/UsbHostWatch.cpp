#include "UsbHostWatch.h"

#include <Arduino.h>
#include <USB.h>

namespace usb {

namespace {
volatile bool     g_connected = false;
volatile uint32_t g_connectedAtMs = 0;

void onUsbEvent(void* /*arg*/, esp_event_base_t base, int32_t id, void* /*data*/) {
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT:
        case ARDUINO_USB_RESUME_EVENT:
            if (!g_connected) g_connectedAtMs = millis();
            g_connected = true;
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
        case ARDUINO_USB_STOPPED_EVENT:
            g_connected = false;
            break;
        default: break;
    }
}
}

void watchHostEvents() {
    USB.onEvent(onUsbEvent);
}

bool hostConnected()        { return g_connected; }
uint32_t hostConnectedAtMs() { return g_connectedAtMs; }

} // namespace usb
