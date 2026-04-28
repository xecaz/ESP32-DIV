#pragma once

#include <stdint.h>

namespace radio {

// Identifies which physical radio a feature wants. The manager grants at
// most one RF owner at a time (other than the always-available WiFi/BLE
// stacks in the ESP32-S3 itself) and enforces pin-conflict exclusion
// between the NRF24 #3 slot and IR (they share GPIO 14/21).
enum class Owner : uint8_t {
    None = 0,
    Wifi,     // built-in, uses no shared pins
    Ble,      // built-in, uses no shared pins
    Nrf24,    // any/all of the 3 NRF24 modules
    Cc1101,   // sub-GHz transceiver
    Ir,       // IR TX/RX (conflicts with NRF24 #3)
};

const char* ownerName(Owner o);

// Try to become the active owner. Returns false if another non-compatible
// owner is already active. Safe to call from any task.
bool acquire(Owner o);

// Release ownership. Only the current owner may release.
void release(Owner o);

// Read-only probe of who owns the bus right now.
Owner currentOwner();

// One-shot init called from setup() after board::init().
void init();

} // namespace radio
