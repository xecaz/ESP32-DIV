#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace radio {
namespace ble_adv {

// Runs BLE advertisements in a dedicated FreeRTOS task so the UI keeps
// responding. Used by two features:
//   - Spoofer:   loops through 17 Apple device advertisement payloads
//   - SourApple: generates a random malformed AirPods-class packet that
//                has been reported to crash the BLE stacks of some Apple
//                devices (for defensive research only).
enum class Mode : uint8_t {
    Off = 0,
    SpoofApple,
    SourApple,
};

// Start (or switch) the advertiser. Returns false if another radio owns
// the chip. For SpoofApple, deviceIndex selects which of the 17 preset
// payloads to broadcast (0..16); cycling is the caller's job. For
// SourApple, deviceIndex is ignored (a new random packet is generated
// per advertisement).
bool start(Mode m, uint8_t deviceIndex = 0);

// Stop advertising and release the BLE radio.
void stop();

Mode    currentMode();
uint8_t currentSpoofIndex();

// For Spoofer UI: cycle to the next / previous preset and reapply.
void spoofNext();
void spoofPrev();

// Number of advertisements emitted since start — counter the UI can poll
// to show "we're actually doing something."
uint32_t txCount();

// Human-readable name of the current spoof preset (e.g. "AirPods Pro").
const char* spoofName(uint8_t index);

} // namespace ble_adv
} // namespace radio
