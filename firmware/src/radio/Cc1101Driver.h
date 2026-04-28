#pragma once

#include <Arduino.h>   // String
#include <stdint.h>

namespace radio {
namespace cc1101 {

// Sub-GHz transceiver on the shared SPI bus. Uses RCSwitch for OOK
// encode/decode so protocol-coded remotes (garage doors, cheap 433 MHz
// controllers) round-trip cleanly. Raw capture is a future extension.

struct Capture {
    uint32_t freqHz;
    uint32_t value;     // RCSwitch decoded value
    uint16_t bitLength;
    uint16_t protocol;  // RCSwitch protocol # (1..N)
};

// Replay attack: SEL to start listening, SEL again once captured to replay.
bool startRx(uint32_t freqHz);
void stopRx();
bool rxRunning();
bool rxHaveCapture();
bool rxLatestCapture(Capture& out);

bool txCapture(const Capture& c);   // transmit once

// Jammer: continuous TX on the given frequency.
bool startJammer(uint32_t freqHz);
void stopJammer();
bool jammerRunning();

// ── Saved profiles ─────────────────────────────────────────────────────
// Binary records at /subghz/<label>.bin. Same pattern as IR: the label
// becomes the filename (slugified + unique-suffixed on collision).
struct __attribute__((packed)) SavedHeader {
    uint32_t magic;       // 'SGPF'
    uint16_t version;     // 1
    uint16_t reserved;
    uint32_t frequency;   // Hz
    uint32_t value;       // RCSwitch decoded value
    uint16_t bitLength;
    uint16_t protocol;    // RCSwitch protocol #
    char     name[20];
};

String saveCaptureToSd(const Capture& c, const char* label);
int    listSaved(String* out, int max);
bool   loadCapture(const String& basename, Capture& out);
bool   deleteSaved(const String& basename);

} // namespace cc1101
} // namespace radio
