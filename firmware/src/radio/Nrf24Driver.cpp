#include "Nrf24Driver.h"

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "../hw/Board.h"
#include "../hw/Pins.h"
#include "../hw/Leds.h"
#include "../storage/Settings.h"
#include "RadioManager.h"

namespace radio {
namespace nrf24 {

namespace {

// The RF24 library constructs its own SPIClass internally (on ESP32 it
// uses the HSPI/SPI2 class). Our shared bus lives on SPI_MOSI/MISO/SCLK
// (11/13/12). We pass the CE/CSN pins at construction; RF24 toggles CE
// and handles CSN assertion per-transaction.
//
// Speed: 4 MHz. The stock firmware runs the scanner at 10 MHz on the same
// traces, but our bus is shared with SD + CC1101 + TFT touch — going slower
// costs nothing (a sweep is still sub-50 ms) and is more forgiving of the
// longer trace lengths and reflections on this board.
RF24 g_r1(pins::NRF24_1_CE, pins::NRF24_1_CSN, 4000000);
RF24 g_r2(pins::NRF24_2_CE, pins::NRF24_2_CSN, 4000000);
RF24 g_r3(pins::NRF24_3_CE, pins::NRF24_3_CSN, 4000000);

// Channels ported from cifertech's BleJammer: three BLE advertising
// channels split across three modules so we're always jamming all three
// at once with extra harmonics nearby.
const uint8_t BLE_GROUP_1[] = { 2,  5,  8, 11 };
const uint8_t BLE_GROUP_2[] = {26, 29, 32, 35 };
const uint8_t BLE_GROUP_3[] = {80, 83, 86, 89 };

// Classic Bluetooth hops (ports the stock firmware's 21-channel table).
const uint8_t CLASSIC_GROUP_1[] = { 0,  1,  2,  4,  6,  8, 22 };
const uint8_t CLASSIC_GROUP_2[] = {24, 26, 28, 30, 32, 34, 46 };
const uint8_t CLASSIC_GROUP_3[] = {48, 50, 52, 74, 76, 78, 80 };

// Protokill: a wideband spread across 2.4 GHz that clobbers WiFi (6, 30-55),
// Zigbee (10, 15, 20, 25) and BLE (2, 26, 80) at once. Three modules so we
// cover ~30 distinct channels simultaneously.
const uint8_t PROTOKILL_GROUP_1[] = {  6, 10, 14, 18, 22, 26, 30, 34, 38, 42 };
const uint8_t PROTOKILL_GROUP_2[] = { 46, 50, 54, 58, 62, 66, 70, 74, 78, 82 };
const uint8_t PROTOKILL_GROUP_3[] = {  2, 12, 20, 28, 40, 48, 56, 72, 80, 90 };

bool        g_jammer   = false;
JamBand     g_band     = JamBand::Ble;
TaskHandle_t g_jamTask = nullptr;
bool        g_stopJam  = false;

bool        g_spectrum = false;
TaskHandle_t g_spTask  = nullptr;
bool        g_stopSp   = false;
uint8_t     g_spMap[SPECTRUM_CHANNELS] = {};
SemaphoreHandle_t g_spMutex = nullptr;

void configureJammer(RF24& radio, const uint8_t* channels, size_t n) {
    if (!radio.begin(&board::busSpi())) return;
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    for (size_t i = 0; i < n; ++i) {
        radio.setChannel(channels[i]);
        radio.startConstCarrier(RF24_PA_MAX, channels[i]);
    }
}

void jammerTask(void*) {
    const uint8_t *g1, *g2, *g3;
    size_t n;
    switch (g_band) {
        case JamBand::Ble:
            g1 = BLE_GROUP_1; g2 = BLE_GROUP_2; g3 = BLE_GROUP_3;
            n  = sizeof(BLE_GROUP_1); break;
        case JamBand::Classic:
            g1 = CLASSIC_GROUP_1; g2 = CLASSIC_GROUP_2; g3 = CLASSIC_GROUP_3;
            n  = sizeof(CLASSIC_GROUP_1); break;
        case JamBand::Protokill:
        default:
            g1 = PROTOKILL_GROUP_1; g2 = PROTOKILL_GROUP_2; g3 = PROTOKILL_GROUP_3;
            n  = sizeof(PROTOKILL_GROUP_1); break;
    }

    configureJammer(g_r1, g1, n);
    configureJammer(g_r2, g2, n);
    // Only drive radio 3 if the shield profile says NRF24 #3 is populated.
    if (storage::get().shield == storage::ShieldProfile::Nrf24Triple) {
        configureJammer(g_r3, g3, n);
    }

    // Jamming is now autonomous inside the nRF24 radios — our task just
    // monitors the stop flag and exits on request. Pulse whatever LEDs
    // correspond to the *bands* being clobbered (not just the NRF24
    // pixel that represents the transmitting hardware), so the user can
    // see at a glance which spectra are currently being jammed.
    //   BLE jam        → Ble + Nrf24 pixels
    //   Classic BT jam → Ble + Nrf24 pixels  (classic BT lives on the BLE LED)
    //   Protokill      → Wifi + Ble + Nrf24 (it's 2.4 GHz wideband)
    while (!g_stopJam) {
        leds::signal(leds::Channel::Nrf24, leds::Event::Tx);
        if (g_band == JamBand::Ble || g_band == JamBand::Classic ||
            g_band == JamBand::Protokill) {
            leds::signal(leds::Channel::Ble, leds::Event::Tx);
        }
        if (g_band == JamBand::Protokill) {
            leds::signal(leds::Channel::Wifi, leds::Event::Tx);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    g_r1.powerDown();
    g_r2.powerDown();
    g_r3.powerDown();
    g_jamTask = nullptr;
    vTaskDelete(nullptr);
}

void spectrumTask(void*) {
    // Enumerate which NRF24 slots actually respond — not every shield has
    // all three populated, and the IR/NRF24 #3 conflict makes U3 optional
    // even when the shield is Nrf24Triple. Every responder joins the sweep.
    //
    // With N radios we split the 0..125 channel range into N interleaved
    // sets: radio k handles {ch : ch % N == k}. On each hop we program all
    // active radios (fast, microseconds), then share a single 300 µs RPD
    // dwell across every module at once — so a 3-radio sweep advances 3
    // channels per dwell and finishes in ~1/3 the time.
    struct Slot { RF24* r; const char* name; };
    Slot all[] = {
        {&g_r1, "U1"},
        {&g_r2, "U2"},
        {&g_r3, "U3"},
    };
    RF24* active[3] = { nullptr, nullptr, nullptr };
    int activeCount = 0;
    bool tripleOk = (storage::get().shield == storage::ShieldProfile::Nrf24Triple);
    for (int i = 0; i < 3; ++i) {
        if (i == 2 && !tripleOk) break;
        bool ok = all[i].r->begin(&board::busSpi());
        bool chip = ok && all[i].r->isChipConnected();
        Serial.printf("[nrf24] spectrum %s begin=%d chip=%d%s\n",
                      all[i].name, (int)ok, (int)chip,
                      chip ? " [active]" : "");
        if (chip) active[activeCount++] = all[i].r;
        else      all[i].r->powerDown();
    }
    if (activeCount == 0) {
        Serial.println("[nrf24] spectrum: no NRF24 module responds — check shield");
        g_spTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    // Block-split the ISM band (NRF channels 0..83) so the modules sit
    // far apart in frequency during each dwell. An earlier interleaved
    // split (radios on c, c+1, c+2 simultaneously) bled each module's LO
    // into its neighbour's RPD and painted the whole band "active" even
    // with no real traffic. With a block split, 3 radios are ~28 MHz apart
    // — well outside the ~2 MHz NRF24 receive bandwidth, so LO leakage no
    // longer trips neighbouring modules.
    const int SLICE = (SPECTRUM_CHANNELS + activeCount - 1) / activeCount;
    int base[3] = {0, SLICE, SLICE * 2};
    Serial.printf("[nrf24] spectrum: %d module(s) active, block-split %d chans each (total %d)\n",
                  activeCount, SLICE, SPECTRUM_CHANNELS);

    // Bring each active module into PRX mode once. Channel programming
    // requires CE=LOW; per-hop code strobes CE high for the dwell window.
    for (int i = 0; i < activeCount; ++i) {
        active[i]->setAutoAck(false);
        active[i]->setPALevel(RF24_PA_MIN);
        active[i]->setDataRate(RF24_2MBPS);
        active[i]->setCRCLength(RF24_CRC_DISABLED);
        active[i]->setAddressWidth(3);
        active[i]->startListening();
        active[i]->ce(LOW);
    }

    uint8_t local[SPECTRUM_CHANNELS];
    while (!g_stopSp) {
        memset(local, 0, sizeof(local));
        // Advance in lockstep through each module's slice. Module i
        // samples base[i]..base[i]+SLICE-1, clamped to SPECTRUM_CHANNELS-1.
        for (int h = 0; h < SLICE && !g_stopSp; ++h) {
            int ch[3] = {-1, -1, -1};
            for (int i = 0; i < activeCount; ++i) {
                int c = base[i] + h;
                if (c >= SPECTRUM_CHANNELS) continue;
                ch[i] = c;
                active[i]->setChannel(c);
                active[i]->ce(HIGH);
            }
            // Single 300 µs window shared across every module — they sample
            // their assigned channels in parallel during this dwell.
            delayMicroseconds(300);
            for (int i = 0; i < activeCount; ++i) {
                if (ch[i] < 0) continue;
                if (active[i]->testRPD()) local[ch[i]] = 1;
                active[i]->ce(LOW);
            }
        }
        if (xSemaphoreTake(g_spMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            memcpy(g_spMap, local, sizeof(local));
            xSemaphoreGive(g_spMutex);
        }
        for (int i = 0; i < SPECTRUM_CHANNELS; ++i) {
            if (local[i]) { leds::signal(leds::Channel::Nrf24, leds::Event::Rx); break; }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    for (int i = 0; i < activeCount; ++i) {
        active[i]->stopListening();
        active[i]->powerDown();
    }
    g_spTask = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void init() {
    if (!g_spMutex) g_spMutex = xSemaphoreCreateMutex();
    // No boot-time probe: the spectrum task already enumerates which
    // modules respond on entry, and the boot probe was 3× SPI begin()
    // calls (each ~5 ms internal delay + register reads) running before
    // the UI task started — measurable boot delay for no benefit.
}

bool startJammer(JamBand band) {
    if (!radio::acquire(radio::Owner::Nrf24)) return false;
    if (g_jammer) return true;
    g_band    = band;
    g_jammer  = true;
    g_stopJam = false;
    xTaskCreatePinnedToCore(jammerTask, "nrf_jam", 4096, nullptr,
                            /*prio=*/1, &g_jamTask, /*coreId=*/1);
    return true;
}

void stopJammer() {
    if (!g_jammer) return;
    g_stopJam = true;
    for (int i = 0; i < 20 && g_jamTask; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    g_jammer = false;
    // The jammer task pulses Ble (BLE/Classic jam) and additionally Wifi
    // (Protokill) alongside its own Nrf24 pixel. radio::release() only
    // knows to blank the primary owner's LED, so clear the extras here.
    leds::blank(leds::Channel::Ble);
    leds::blank(leds::Channel::Wifi);
    radio::release(radio::Owner::Nrf24);
}

bool     jammerRunning() { return g_jammer; }
JamBand  jammerBand()    { return g_band; }

bool startSpectrum() {
    if (!radio::acquire(radio::Owner::Nrf24)) return false;
    if (g_spectrum) return true;
    if (!g_spMutex) g_spMutex = xSemaphoreCreateMutex();
    g_spectrum = true;
    g_stopSp   = false;
    xTaskCreatePinnedToCore(spectrumTask, "nrf_spec", 4096, nullptr,
                            /*prio=*/1, &g_spTask, /*coreId=*/1);
    return true;
}

void stopSpectrum() {
    if (!g_spectrum) return;
    g_stopSp = true;
    for (int i = 0; i < 20 && g_spTask; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    g_spectrum = false;
    radio::release(radio::Owner::Nrf24);
}

bool spectrumRunning() { return g_spectrum; }

bool spectrumSnapshot(uint8_t out[SPECTRUM_CHANNELS]) {
    if (!g_spMutex) return false;
    if (xSemaphoreTake(g_spMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    memcpy(out, g_spMap, SPECTRUM_CHANNELS);
    xSemaphoreGive(g_spMutex);
    return true;
}

} // namespace nrf24
} // namespace radio
