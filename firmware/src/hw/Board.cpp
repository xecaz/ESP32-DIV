#include "Board.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Pins.h"
#include "../radio/RadioManager.h"

namespace board {

TFT_eSPI tft;

namespace {
constexpr int BACKLIGHT_CHANNEL    = 0;
constexpr int BACKLIGHT_PWM_FREQ   = 5000;
constexpr int BACKLIGHT_PWM_BITS   = 8;

SPIClass g_busSpi(FSPI);   // shared bus for SD / CC1101 / NRF24
bool     g_sdMounted = false;
bool     g_busBegun  = false;
}

SPIClass& busSpi() { return g_busSpi; }

void setBacklight(uint8_t level) {
    ledcWrite(BACKLIGHT_CHANNEL, level);
}

bool sdMounted() { return g_sdMounted; }

bool mountSd() {
    if (g_sdMounted) return true;
    if (!g_busBegun) {
        g_busSpi.begin(pins::SPI_SCLK, pins::SPI_MISO, pins::SPI_MOSI, pins::SD_CS);
        g_busBegun = true;
    }
    // Single SD.begin at a moderate speed — fast enough for typical cards,
    // conservative enough for cheap/large ones. One-shot (no speed
    // fallback loop) so boot isn't stalled by a missing card and the
    // storage task's periodic retry does the rest.
    uint32_t t0 = millis();
    bool ok = SD.begin(pins::SD_CS, g_busSpi, 10000000);
    uint32_t dur = millis() - t0;
    if (ok) {
        g_sdMounted = true;
        Serial.printf("[sd] mount OK type=%u size=%lluMB (took %lums)\n",
                      SD.cardType(),
                      SD.cardSize() / (1024ULL * 1024ULL),
                      (unsigned long)dur);
        return true;
    }
    // Log slow failures so we can see if a present-but-stubborn card is
    // burning bus time on every retry.
    if (dur > 200) {
        Serial.printf("[sd] mount FAIL took %lums\n", (unsigned long)dur);
    }
    return false;
}

void unmountSd() {
    if (!g_sdMounted) return;
    SD.end();
    g_sdMounted = false;
}

bool sdCardPresent() {
    // SD_CD is INPUT_PULLUP (set in init()). Most breakouts short SD_CD to
    // GND when a card is physically seated, so LOW = present.
    return digitalRead(pins::SD_CD) == LOW;
}

bool sdPollHotplug() {
    bool present = sdCardPresent();
    if (present && !g_sdMounted) {
        bool ok = mountSd();
        return ok;   // state changed iff the mount actually succeeded
    }
    if (!present && g_sdMounted) {
        unmountSd();
        return true;
    }
    return false;
}

namespace {

// True if a SPI-sharing radio currently owns the bus. Storage task must
// back off — doing SD.begin() or SD.open() while CC1101 or an NRF24 has
// the bus reconfigured deadlocks: both parties spin on the same wires.
bool spiBusy() {
    auto o = radio::currentOwner();
    return o == radio::Owner::Cc1101 || o == radio::Owner::Nrf24;
}

void storageTaskEntry(void*) {
    // Background SD mount. Each failed SD.begin takes ~1 s, so we want
    // to retry often enough that a freshly inserted card mounts within
    // a few seconds, but not so often that we burn FSPI bus time when
    // no card is in.
    //
    // Strategy:
    //   - Boot: short stagger (300 ms) so the splash + UI tasks render
    //     before the first 1-second SD.begin, but the user doesn't
    //     have to wait long for SD-dependent screens to come alive.
    //   - On failure: backoff 2 → 4 → 8 → 16 → 30 s (capped). Quick
    //     enough to catch most "card just got seated" scenarios.
    //   - SD_CD edge detection: any change on the card-detect line
    //     (insertion or removal) resets the backoff so re-insertion
    //     mounts within a poll period.
    //
    // SD_CD polling: every 250 ms (cheap GPIO read). The mount-attempt
    // gate uses its own deadline so we don't accidentally stall every
    // 250 ms on a 1 s SD.begin.
    vTaskDelay(pdMS_TO_TICKS(300));
    uint32_t backoffMs     = 2000;
    uint32_t nextAttemptMs = 0;
    bool     lastPresent   = sdCardPresent();
    for (;;) {
        // CD edge detection — wakes us up on insertion/removal.
        bool present = sdCardPresent();
        if (present != lastPresent) {
            lastPresent = present;
            backoffMs = 2000;
            nextAttemptMs = 0;
            if (!present && g_sdMounted) unmountSd();
        }

        bool mounted = g_sdMounted;
        if (!spiBusy() && !mounted &&
            (int32_t)(millis() - nextAttemptMs) >= 0) {
            if (mountSd()) {
                backoffMs = 2000;
            } else {
                if (backoffMs < 30000) backoffMs *= 2;
                if (backoffMs > 30000) backoffMs = 30000;
            }
            nextAttemptMs = millis() + backoffMs;
        }
        vTaskDelay(pdMS_TO_TICKS(mounted ? 5000 : 250));
    }
}
}

void startStorageTask() {
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreatePinnedToCore(storageTaskEntry, "storage", 4096, nullptr,
                            /*prio=*/1, nullptr, /*coreId=*/1);
}

void init() {
    // Backlight PWM — off initially, ramp up after TFT init.
    pinMode(pins::DISPLAY_BACKLIGHT, OUTPUT);
    ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_PWM_FREQ, BACKLIGHT_PWM_BITS);
    ledcAttachPin(pins::DISPLAY_BACKLIGHT, BACKLIGHT_CHANNEL);
    setBacklight(0);

    // TFT init (pins configured via platformio.ini build flags).
    // Rotation 2 = portrait, flipped 180° — matches the board's physical
    // orientation (power + USB at the bottom, not the top).
    tft.init();
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);

    setBacklight(200);

    // I²C for PCF8574 button expander on bodged GPIO 41/42 (Pins.h). Wire
    // is back: bit-bang attempts produced corrupted reads on this bus
    // (presses registering as the wrong direction). Wire's occasional
    // 1-second stalls are the lesser evil.
    // 50 kHz instead of 100 kHz: doubles the SCL half-cycle, giving slow
    // edges (which the bodged SDA/SCL wires create) time to actually
    // settle high before the slave samples. The PCF8574 spec says
    // 0–100 kHz so this is well within tolerances. Halving the rate
    // measurably reduces single-bit-shifted reads on this board.
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, /*freq=*/50000);
    Wire.setTimeOut(50);

    // Buzzer was desoldered on this device (it shipped buzzing
    // continuously); no point driving GPIO 2 anywhere. Battery ADC stays.
    analogReadResolution(12);

    // Bring the shared SPI bus up here (not lazily in mountSd) so radios can
    // use it even when no SD card is present. RF24 in particular will call
    // begin() on whichever SPIClass we hand it; it must already be attached
    // to the right pins.
    if (!g_busBegun) {
        g_busSpi.begin(pins::SPI_SCLK, pins::SPI_MISO, pins::SPI_MOSI, pins::SD_CS);
        g_busBegun = true;
    }

    // (No boot-time neopixel blank — leds::init() runs ~ms later from
    // main.cpp and emits the same "all off" frame. Doing it twice via
    // RMT seems to perturb I²C timing on this board: the PCF8574 reads
    // get a 1-bit right shift, registering SELECT as DOWN. Just letting
    // leds::init handle it once eliminates the symptom.)

    // Park every peripheral CS on the shared SPI bus HIGH AFTER the
    // shotgun. The CANDIDATE_PINS list above includes GPIO 48, which is
    // NRF24_2_CSN on this board — so the WS2812 blanker leaves CSN_2 as a
    // GPIO output LOW, which means NRF24 #2 sits on the bus actively
    // chip-selected and corrupts every subsequent SD transaction. Doing
    // the parking *after* the shotgun is the only ordering that sticks.
    for (int cs : { pins::CC1101_CS,
                    pins::NRF24_1_CSN,
                    pins::NRF24_2_CSN,
                    pins::NRF24_3_CSN }) {
        pinMode(cs, OUTPUT);
        digitalWrite(cs, HIGH);
    }

    // Card-detect as input-pullup so SD hotplug detection works later.
    pinMode(pins::SD_CD, INPUT_PULLUP);

    // SD mount is deferred to the storage task (see startStorageTask). On
    // this board a failed SD.begin takes ~1 s, so doing it synchronously
    // here would freeze boot — the user would see the splash for a beat
    // longer with no benefit. The storage task waits for the UI to come
    // up, then makes ONE attempt at a low-priority moment.
}

} // namespace board
