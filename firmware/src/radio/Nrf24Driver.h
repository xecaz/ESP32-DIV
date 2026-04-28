#pragma once

#include <stdint.h>

namespace radio {
namespace nrf24 {

// Three NRF24L01+ modules sharing the main SPI bus (11/12/13) with
// separate CE/CSN pins per module. Used for 2.4 GHz jamming, sniffing,
// and the spectrum scanner.

// ── Jammer (constant carrier sweep) ─────────────────────────────────────
// Two target bands pre-defined:
//   Ble      — 3 BLE advertising channels on each of the 3 modules = 12 ch
//   Classic  — wideband 2.4 GHz channel sweep across all 3 modules
enum class JamBand : uint8_t {
    Ble,        // BLE advertising channels + harmonics
    Classic,    // Bluetooth Classic hops
    Protokill,  // Wideband: WiFi + BLE + Zigbee + random in one sweep
};

// Start (or switch) the jammer. Returns false if another radio owns the
// shared SPI bus, or if the user's ShieldProfile forbids NRF24 #3 (in
// which case only the first two radios are driven).
bool startJammer(JamBand band);

// Stop the jammer, power-down all NRF24s, release the bus.
void stopJammer();

bool     jammerRunning();
JamBand  jammerBand();

// ── Spectrum scanner ────────────────────────────────────────────────────
// Sweeps the 2.4 GHz ISM band, noting each channel's activity via the
// NRF24 RPD (Received Power Detector) bit. Covers NRF24 channels 0..83
// (2.400–2.483 GHz) — everything above 83 is out-of-band and just wastes
// screen + dwell time on frequencies nothing transmits on legally.
constexpr int SPECTRUM_CHANNELS = 84;

bool startSpectrum();
void stopSpectrum();
bool spectrumRunning();

// Copy the latest SPECTRUM_CHANNELS-entry RPD map into `out`. Values are
// 0 (quiet) or 1 (detected). Returns true if data was available.
bool spectrumSnapshot(uint8_t out[SPECTRUM_CHANNELS]);

// One-time init (called from radio::init()).
void init();

} // namespace nrf24
} // namespace radio
