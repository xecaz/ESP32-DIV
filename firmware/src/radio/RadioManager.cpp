#include "RadioManager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Nrf24Driver.h"
#include "../hw/Board.h"
#include "../hw/Leds.h"
#include "../storage/Settings.h"

namespace radio {

namespace {
SemaphoreHandle_t g_mutex = nullptr;
Owner             g_owner = Owner::None;

// NRF24 #3 and IR share pins 14/21. Only one is physically populated on
// any given shield, so either driver is safe to bring up as long as the
// other is not currently holding the radio-manager slot. A Settings screen
// (M10) will later let the user pin this down explicitly if they want the
// strict exclusion back.
bool compatibleWithProfile(Owner /*want*/) { return true; }
} // namespace

void init() {
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
    nrf24::init();
}

const char* ownerName(Owner o) {
    switch (o) {
        case Owner::None:   return "-";
        case Owner::Wifi:   return "WIFI";
        case Owner::Ble:    return "BLE";
        case Owner::Nrf24:  return "NRF24";
        case Owner::Cc1101: return "CC1101";
        case Owner::Ir:     return "IR";
    }
    return "?";
}

bool acquire(Owner o) {
    if (!g_mutex) init();
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) return false;

    bool ok = false;
    if (g_owner == Owner::None && compatibleWithProfile(o)) {
        g_owner = o;
        ok = true;
    } else if (g_owner == o) {
        ok = true; // re-acquire by same owner is a no-op
    }

    xSemaphoreGive(g_mutex);
    return ok;
}

void release(Owner o) {
    if (!g_mutex) return;
    if (xSemaphoreTake(g_mutex, portMAX_DELAY) != pdTRUE) return;
    bool wasOurs = (g_owner == o);
    if (wasOurs) g_owner = Owner::None;
    xSemaphoreGive(g_mutex);

    // SD, CC1101, and the NRF24s share SPI bus pins 11/12/13. After a
    // radio releases, the bus config is still whatever the radio last
    // set it to — SD's next operation would fail. Just mark the mount
    // stale; the storage task's next poll re-runs SD.begin() and gets
    // the bus back to SD timings. No synchronous work here (doing it
    // here blocked the UI task and tripped the watchdog on back-to-back
    // feature switches).
    if (wasOurs && (o == Owner::Cc1101 || o == Owner::Nrf24)) {
        board::unmountSd();
    }

    // Blank the LED this radio was driving so the last tx/rx flash doesn't
    // cling after the user backs out of a feature. Ir has no pixel.
    if (wasOurs) {
        switch (o) {
            case Owner::Wifi:   leds::blank(leds::Channel::Wifi);   break;
            case Owner::Ble:    leds::blank(leds::Channel::Ble);    break;
            case Owner::Cc1101: leds::blank(leds::Channel::Subghz); break;
            case Owner::Nrf24:  leds::blank(leds::Channel::Nrf24);  break;
            default: break;
        }
    }
}

Owner currentOwner() { return g_owner; }

} // namespace radio
