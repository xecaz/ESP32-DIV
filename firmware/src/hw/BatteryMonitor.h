#pragma once

#include <stdint.h>

// Battery monitor: reads VBAT through the on-board R11/R16 (100k/100k) divider
// on GPIO 2 (ADC1_CH1) and maps it to a percentage with a LiPo discharge curve.
//
// This is deliberately INDEPENDENT of the IP5306 I²C gauge — that gauge is
// coarse (25 % buckets), often unreadable, and is the source of the bogus
// hardcoded "70 %". A calibrated analog read of VBAT is finer and always
// available. ADC1 is used (works while WiFi is active, unlike ADC2).
namespace battery {

// Configure the ADC pin and load the calibration factor. Call once after
// Board::init() (which sets analogReadResolution).
void begin();

// Sample + filter. Call periodically (~1–2 s is plenty).
void update();

uint16_t millivolts();   // filtered VBAT in mV (0 until first update)
uint8_t  percent();      // 0..100 from the LiPo curve
uint16_t rawPinMv();     // last raw ADC reading AT THE PIN (for calibration)

// True if a cell appears to be connected. Heuristic: VBAT sits in a plausible
// single-cell window. NOTE: if a battery-less board on USB floats VBAT up near
// the charge voltage, this can't distinguish it from a full cell — the window
// thresholds may need tuning against measured battery-out readings.
bool present();

// Divider/scale trim (VBAT = pin_mV × calibration). Default 2.00 (the nominal
// 100k/100k ratio); adjust to correct for resistor tolerance + ADC scale,
// persisted to NVS.
float calibration();
void  setCalibration(float k);

} // namespace battery
