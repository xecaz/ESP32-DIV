#include "Leds.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "../storage/Settings.h"

namespace leds {

namespace {
constexpr int NUM_PIXELS = 4;

Adafruit_NeoPixel* g_strip   = nullptr;
uint8_t            g_pin     = 0;
uint8_t            g_bright  = 0;
bool               g_enabled = false;

// Last "activity" timestamp per channel so idle fades naturally instead
// of snapping to black mid-blink.
uint32_t g_eventUntilMs[NUM_PIXELS] = {};
uint32_t g_eventColor[NUM_PIXELS]   = {};

uint32_t colorFor(Channel ch, Event ev) {
    const auto& s = storage::get();
    switch (ch) {
        case Channel::Wifi:   return ev == Event::Rx ? s.ledRxWifi   : s.ledTxWifi;
        case Channel::Ble:    return ev == Event::Rx ? s.ledRxBle    : s.ledTxBle;
        case Channel::Subghz: return ev == Event::Rx ? s.ledRxSubghz : s.ledTxSubghz;
        case Channel::Nrf24:  return ev == Event::Rx ? s.ledRxNrf24  : s.ledTxNrf24;
    }
    return 0;
}

// Write the strip with current per-pixel event state.
void render() {
    if (!g_strip || !g_enabled) return;
    uint32_t now = millis();
    for (int i = 0; i < NUM_PIXELS; ++i) {
        if (now < g_eventUntilMs[i]) {
            g_strip->setPixelColor(i, g_eventColor[i]);
        } else {
            g_strip->setPixelColor(i, 0);
        }
    }
    g_strip->show();
}

} // namespace

void init() {
    const auto& s = storage::get();
    if (g_strip && (s.ledPin != g_pin || s.ledBrightness != g_bright)) {
        delete g_strip;
        g_strip = nullptr;
    }
    if (!g_strip) {
        g_pin    = s.ledPin;
        g_bright = s.ledBrightness;
        g_strip  = new Adafruit_NeoPixel(NUM_PIXELS, g_pin,
                                         NEO_GRB + NEO_KHZ800);
        g_strip->begin();
        g_strip->setBrightness(g_bright);
        g_strip->clear();
        g_strip->show();
    }
    g_enabled = s.ledEnabled;
    if (!g_enabled) allOff();
}

void reload() { init(); }

void signal(Channel ch, Event ev) {
    if (!g_enabled || !g_strip) return;
    if (ev == Event::Idle) return;
    int idx = (int)ch;
    g_eventColor[idx]    = colorFor(ch, ev);
    g_eventUntilMs[idx]  = millis() + 120;   // flash duration
    render();
}

void setAll(uint32_t rgb) {
    if (!g_strip) return;
    for (int i = 0; i < NUM_PIXELS; ++i) g_strip->setPixelColor(i, rgb);
    g_strip->show();
}

void runTest() {
    if (!g_strip) init();
    if (!g_strip) return;
    const Channel chans[] = { Channel::Wifi, Channel::Ble,
                              Channel::Subghz, Channel::Nrf24 };
    // Walk RX then TX, lighting each channel's pixel while dimming the rest.
    for (Event ev : { Event::Rx, Event::Tx }) {
        for (auto c : chans) {
            g_strip->clear();
            g_strip->setPixelColor((int)c, colorFor(c, ev));
            g_strip->show();
            delay(300);
        }
    }
    g_strip->clear();
    g_strip->show();
    memset(g_eventUntilMs, 0, sizeof(g_eventUntilMs));
}

void allOff() {
    if (!g_strip) return;
    g_strip->clear();
    g_strip->show();
    memset(g_eventUntilMs, 0, sizeof(g_eventUntilMs));
}

void blank(Channel ch) {
    if (!g_strip) return;
    int idx = (int)ch;
    if (idx < 0 || idx >= NUM_PIXELS) return;
    g_eventUntilMs[idx] = 0;  // clear any pending fade
    g_eventColor[idx]   = 0;
    g_strip->setPixelColor(idx, 0);
    g_strip->show();
}

} // namespace leds
