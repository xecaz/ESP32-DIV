#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace radio {

// Thin wrapper around the built-in WiFi scanner so the rest of the code
// doesn't depend on Arduino's WiFi API directly. Everything here is
// non-blocking: the UI polls results on each tick.

struct ScanEntry {
    String   ssid;
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  encType;   // WIFI_AUTH_* enum from Arduino WiFi.h
};

namespace wifi {

// Kick off (or restart) an async scan. Returns false if another radio owns
// the device. Idempotent — calling while a scan is running is a no-op.
bool startScan();

// Returns true once the most recent startScan() has results available.
bool scanDone();

// Returns true while a scan is actively running on the radio.
bool scanRunning();

// Number of networks found by the last completed scan (0 if none yet).
int  resultCount();

// Fill `out` with entry at index. Returns false if index out of range or
// no scan has completed.
bool resultAt(int index, ScanEntry& out);

// Release the WiFi radio — stops scan, frees the radio::Owner::Wifi slot.
void stop();

// ── Station-mode + NTP ─────────────────────────────────────────────────
// Connect to the given AP and wait up to `timeoutMs` for an IP. Acquires
// the Wifi radio slot; returns false if another radio owns it, or on
// timeout. On success, the connection stays live until disconnectStation()
// is called or another feature takes over.
bool connectStation(const String& ssid, const String& password,
                    uint32_t timeoutMs = 10000);

// True while a station-mode connection is up.
bool stationConnected();
String stationIp();

// Gracefully disconnect from the AP + release the radio slot.
void disconnectStation();

// Kick off SNTP and wait up to timeoutMs for the clock to sync. Must be
// called while connected (i.e., right after connectStation() returned true).
bool ntpSync(uint32_t timeoutMs = 10000);

} // namespace wifi
} // namespace radio
