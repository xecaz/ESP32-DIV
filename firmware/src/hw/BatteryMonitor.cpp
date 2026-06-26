#include "BatteryMonitor.h"

#include <Arduino.h>
#include <Preferences.h>

#include "Pins.h"

namespace battery {
namespace {

// VBAT = pin_mV × g_cal. The divider is a true ÷2 (R11/R16 = 100k/100k), but
// the ADC reads a stable ~3.2× low at this pin (a fixed load on GPIO2 — not
// S/H settling, which we ruled out), so the empirical scale is ~6.40
// (= metered 4170 mV / 652 mV at the pin). Linear, so one factor calibrates
// the whole range; trim live with Up/Down on the I²C Health screen.
float    g_cal        = 6.40f;
uint16_t g_pinMv      = 0;     // last raw reading at the pin
uint16_t g_vbatMv     = 0;     // filtered VBAT
bool     g_haveSample = false;

// LiPo open-circuit voltage → state-of-charge, single 3.7 V cell. Piecewise
// linear between these (mV, %) points — coarse, but far better than a naive
// linear map or the IP5306's 25 % buckets.
struct Pt { uint16_t mv; uint8_t pct; };
const Pt CURVE[] = {
    {4200,100},{4100, 92},{4000, 83},{3900, 72},{3800, 60},{3750, 52},
    {3700, 43},{3650, 34},{3600, 25},{3500, 14},{3400,  7},{3300,  2},{3000, 0},
};
const int CURVE_N = sizeof(CURVE) / sizeof(CURVE[0]);

uint8_t curvePercent(uint16_t mv) {
    if (mv >= CURVE[0].mv)          return 100;
    if (mv <= CURVE[CURVE_N-1].mv)  return 0;
    for (int i = 0; i < CURVE_N - 1; ++i) {
        if (mv <= CURVE[i].mv && mv >= CURVE[i+1].mv) {
            int dmv = CURVE[i].mv  - CURVE[i+1].mv;
            int dpc = CURVE[i].pct - CURVE[i+1].pct;
            return (uint8_t)(CURVE[i+1].pct +
                   (long)(mv - CURVE[i+1].mv) * dpc / dmv);
        }
    }
    return 0;
}

} // namespace

void begin() {
    analogReadResolution(12);
    // VBAT/2 maxes at ~2.1 V (4.2 V cell); 11 dB attenuation covers it.
    analogSetPinAttenuation(pins::BATTERY_ADC, ADC_11db);
    Preferences p;
    if (p.begin("battery", /*readOnly=*/true)) {
        g_cal = p.getFloat("scale", 6.40f);   // "scale" (new key) ignores any
        p.end();                              // stale "cal" from earlier builds
    }
}

void update() {
    // The R11/R16 (100k/100k) divider presents a ~50 kΩ source, which starves
    // the ADC sample-and-hold so a single read under-reads badly (~3×). Many
    // consecutive reads of the SAME channel let the S/H cap settle toward the
    // true value across reads; we discard the warm-up reads and average the
    // later, settled ones.
    constexpr int WARMUP = 24, KEEP = 8;
    uint32_t acc = 0;
    for (int i = 0; i < WARMUP + KEEP; ++i) {
        uint16_t mv = analogReadMilliVolts(pins::BATTERY_ADC);
        if (i >= WARMUP) acc += mv;
    }
    g_pinMv = (uint16_t)(acc / KEEP);
    uint16_t vbat = (uint16_t)(g_pinMv * g_cal + 0.5f);
    if (!g_haveSample) { g_vbatMv = vbat; g_haveSample = true; }
    else               { g_vbatMv = (uint16_t)((g_vbatMv * 3 + vbat) / 4); }
}

uint16_t millivolts() { return g_vbatMv; }
uint8_t  percent()    { return g_haveSample ? curvePercent(g_vbatMv) : 0; }
uint16_t rawPinMv()   { return g_pinMv; }

bool present() {
    // True battery-absence can't be detected on this board: with no cell the
    // IP5306 floats VBAT to ~4.1 V, identical to a charged battery. So we show
    // the gauge whenever we have any reading; this only suppresses it before
    // the first sample.
    return g_haveSample;
}

float calibration() { return g_cal; }

void setCalibration(float k) {
    g_cal = k;
    Preferences p;
    if (p.begin("battery", /*readOnly=*/false)) {
        p.putFloat("scale", k);
        p.end();
    }
}

} // namespace battery
