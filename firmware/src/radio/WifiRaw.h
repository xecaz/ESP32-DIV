#pragma once

#include <stdint.h>

namespace radio {
namespace wifi_raw {

// Puts the ESP32 WiFi stack into a mode suitable for raw frame transmit
// and promiscuous capture. Claims radio::Owner::Wifi for the duration.
// Stopping releases the radio and switches back to WIFI_OFF.

enum class Mode : uint8_t {
    Off         = 0,
    DeauthDetect,   // promiscuous listener; counts deauth/disassoc frames
    BeaconSpam,     // sends random-SSID beacon frames on channel 1
    DeauthAttack,   // sends deauth frames targeting broadcast
};

bool start(Mode m);
void stop();
Mode currentMode();

// Shared counters for UI display.
uint32_t framesTxd();    // beacon/deauth frames we emitted
uint32_t framesRxd();    // frames we received while detecting
uint32_t deauthsRxd();   // deauth frames we received

} // namespace wifi_raw
} // namespace radio
