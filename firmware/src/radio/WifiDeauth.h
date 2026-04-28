#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace radio {
namespace wifi_deauth {

// Targeted deauthentication driver. Workflow exposed to the UI:
//   1. Scan for APs (reuse radio::wifi::startScan)
//   2. Pick a BSSID + channel → startListen(bssid, channel)
//   3. Poll clientCount() / clientAt() while we discover associated stations
//   4. attack(clientMacOrNullForAll) to start sending deauth frames

struct Client {
    uint8_t  mac[6];
    int8_t   rssi;
    uint32_t packetCount;
    uint32_t lastSeenMs;
};

// Stage 2: passive client discovery. Parks the radio on `channel`,
// promiscuous, filters frames whose BSSID (Addr3) matches `bssid`, and
// collects unique stations. `bssid` / `channel` are remembered for the
// attack stage. Returns false if another radio owns the chip.
bool startListen(const uint8_t bssid[6], uint8_t channel);

// Stop listen or attack — releases the radio.
void stop();

bool   listening();
int    clientCount();
bool   clientAt(int i, Client& out);
void   clearClients();

// Diagnostic counters (helpful when discovery doesn't find anything).
uint32_t totalFramesSeen();    // every mgmt/data frame the radio captured
uint32_t bssidMatchedFrames(); // frames whose BSSID matched the target
uint32_t txErrorCount();       // esp_wifi_80211_tx rejections during attack
int      lastTxError();        // last esp_err_t code (0 = ESP_OK)

// Stage 3: begin the attack. If `targetClient` is null, broadcast deauth
// to FF:FF:FF:FF:FF:FF (kicks every client). Otherwise targets the given
// client MAC. Safe to call while listen is active; it switches modes.
bool attack(const uint8_t* targetClient);  // pass nullptr for broadcast

bool   attacking();
uint32_t txCount();
bool     currentTargetIsBroadcast();
const uint8_t* currentTargetMac();  // valid while attacking(); 6 bytes
const uint8_t* currentBssid();      // 6 bytes
uint8_t  currentChannel();

} // namespace wifi_deauth
} // namespace radio
