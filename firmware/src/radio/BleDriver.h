#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace radio {

struct BleEntry {
    String   name;         // advertised name or "" if none
    uint8_t  mac[6];
    int8_t   rssi;
    uint32_t lastSeenMs;   // millis() when this entry was last updated
    uint16_t packetCount;  // bumped every time we see this MAC again
};

namespace ble {

// Start (or restart) a continuous scan. Devices accumulate in an internal
// table; the UI polls via resultCount()/resultAt(). Returns false if
// another radio owns the chip.
bool startScan();

// Stop the scan and free the radio::Owner::Ble slot.
void stop();

bool    scanRunning();
int     resultCount();
bool    resultAt(int index, BleEntry& out);

// Clear the internal result table (without stopping the scan).
void    clearResults();

} // namespace ble
} // namespace radio
