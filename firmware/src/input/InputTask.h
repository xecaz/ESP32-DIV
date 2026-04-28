#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "InputEvent.h"

namespace input {

// Start the input task. Polls PCF8574 (buttons) and XPT2046 (touch),
// debounces, and pushes Event structs into a queue accessible via queue().
// Safe to call once after Wire.begin() and TFT_eSPI is up.
void start();

// FreeRTOS queue of Event. receiver blocks with xQueueReceive().
QueueHandle_t queue();

// Tuning knobs (compile-time defaults, can be tweaked at runtime later).
struct Tuning {
    // 2 ms poll × streak=2 in InputTask.cpp = 4 ms worst-case press latency.
    // Fast enough that a 5-6 ms finger tap gets at least two "pressed" reads,
    // so the 2-of-2 debounce doesn't eat it.
    uint16_t pollPeriodMs  = 2;
    uint16_t debounceMs    = 4;   // unused by streak debounce; kept for future
    uint16_t longPressMs   = 400; // time before repeat kicks in
    uint16_t repeatMs      = 90;  // auto-repeat interval after long-press
    uint16_t touchPollMs   = 20;  // XPT2046 scan period
    uint16_t touchPressMin = 400; // Z threshold for "touched"
};

Tuning& tuning();

// Most recent raw XPT2046 reading (not pixel-mapped, not rotation-flipped).
// Used by the touch calibration screen.
struct RawTouch { int16_t x, y; uint16_t z; uint32_t tMs; };
RawTouch lastRawTouch();

// PCF8574 I²C bus diagnostics — running counters maintained by the
// background poller task. Used by the I²C-health screen so the user
// can A/B test pull-up changes empirically.
struct PcfStats {
    uint32_t okCount;        // successful Wire.requestFrom calls
    uint32_t failCount;      // failed reads (any error code)
    uint32_t recoverCount;   // pcfRecoverBus invocations
    uint32_t lastOkAgeMs;    // ms since the most recent successful read
    uint8_t  latestRaw;      // most recent raw PCF byte (or 0xFF if none)
};
PcfStats pcfStats();
void     resetPcfStats();   // zero the running counters

} // namespace input
