#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace radio {
namespace wifi_cap {

// WiFi packet monitor: put the radio in promiscuous mode, stream every
// captured frame to an on-SD PCAP file (libpcap format, DLT=IEEE802.11).

bool start();   // opens SD, writes PCAP global header
void stop();    // closes file, disables promiscuous

bool     running();
String   currentFilename();  // empty if not capturing
uint32_t packetsCaptured();
uint32_t bytesWritten();

// 60-second ring buffer of packets-per-second, newest at index (head-1)%N.
// Used by the UI to draw a live rate graph.
constexpr int RATE_HISTORY_LEN = 60;
void     rateHistory(uint16_t out[RATE_HISTORY_LEN]);
uint16_t peakPacketsPerSec();

// Channel hop cadence — the capture task cycles channels 1..11 every
// N ms. The UI can override.
void setChannelHopMs(uint32_t ms);

} // namespace wifi_cap
} // namespace radio
