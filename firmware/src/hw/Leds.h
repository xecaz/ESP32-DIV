#pragma once

#include <stdint.h>

namespace leds {

// 4 WS2812B2020 pixels on the front of the main board. Each slot maps to
// one radio interface so the user can glance at the device and see which
// radio is busy. Default colours are blue for RX, red for TX (same as
// most SDR tools) but everything is customisable in Settings → LEDs.
enum class Channel : uint8_t { Wifi = 0, Ble = 1, Subghz = 2, Nrf24 = 3 };
enum class Event   : uint8_t { Idle, Rx, Tx };

// Start the LED driver. Reads the current settings (pin, brightness,
// enable flag, colours). Safe to call multiple times — reinits if the
// pin or brightness changed.
void init();

// Re-read settings after the user changed them (e.g. picked a new pin or
// switched a colour). Called from the Settings screen.
void reload();

// Flash a channel's RX/TX colour for ~120 ms, then fade back to idle.
// Idle = all off. No-op when ledEnabled is false.
void signal(Channel ch, Event ev);

// Force all 4 pixels to the given RGB (0xRRGGBB) — used by the
// "Identify" button in Settings to verify which LED is which.
void setAll(uint32_t rgb);

// Show each channel's RX + TX colours once so the user can confirm
// their palette. Blocks for ~3 s.
void runTest();

// Turn every LED off, regardless of enable flag. Handy before radio
// features take over a pin.
void allOff();

// Blank one channel's pixel immediately and clear its "still flashing"
// timer. Called from radio::release() so a radio feature's LED stops
// glowing the moment the user exits that screen, instead of clinging to
// the last signal() flash duration.
void blank(Channel ch);

} // namespace leds
