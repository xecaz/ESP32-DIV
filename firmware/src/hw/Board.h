#pragma once

#include <TFT_eSPI.h>

class SPIClass;

namespace board {

// Global TFT instance. Defined in Board.cpp.
extern TFT_eSPI tft;

// Shared SPI bus for SD / CC1101 / NRF24. Pre-configured to use pins::SPI_*.
// Drivers that let us pass an SPIClass (RF24) should hand it this instance
// so they share the same bus — otherwise RF24::begin() silently re-inits
// the default `SPI` on the wrong pins and every transfer returns garbage
// (which is exactly the bug that hid the 2.4 GHz spectrum for months).
SPIClass& busSpi();

// One-shot board bring-up: TFT, backlight PWM, I²C, NeoPixel, buzzer pin,
// battery ADC. Safe to call once from setup() before any UI code.
void init();

// Set backlight 0..255 via LEDC PWM on pins::TFT_BACKLIGHT.
void setBacklight(uint8_t level);

// True while the SD card is mounted on the ESP side.
bool sdMounted();

// Try to (re)mount the SD card. Returns true on success.
bool mountSd();

// Release the SD card (called on hot-removal).
void unmountSd();

// Read the SD_CD card-detect line. Most breakout boards pull it low when
// a card is physically present.
bool sdCardPresent();

// Poll the card-detect line and (un)mount on edge. Cheap — safe to call
// from a UI tick. Returns true if the mount state just changed.
bool sdPollHotplug();

// Start a low-priority background task that keeps retrying mountSd()
// every few seconds until it succeeds. Safe because mount work runs on
// its own task at priority 1 — never preempts UI (prio 2) or input
// (prio 3), but still gets CPU when those yield.
void startStorageTask();

} // namespace board
