#include "BleDriver.h"

#include <NimBLEDevice.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "RadioManager.h"
#include "../hw/Leds.h"

namespace radio {
namespace ble {

namespace {

constexpr int MAX_RESULTS = 64;

BleEntry          g_results[MAX_RESULTS];
int               g_count   = 0;
bool              g_running = false;
SemaphoreHandle_t g_mutex   = nullptr;
NimBLEScan*       g_scan    = nullptr;

bool macEq(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; ++i) if (a[i] != b[i]) return false;
    return true;
}

// Find an existing entry by MAC. Returns index or -1.
int findMac(const uint8_t* mac) {
    for (int i = 0; i < g_count; ++i) {
        if (macEq(g_results[i].mac, mac)) return i;
    }
    return -1;
}

class ScanCb : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        // Rate-limited LED flash — otherwise a dense BLE environment
        // keeps the RX LED solid-on instead of blinking.
        static uint32_t lastFlash = 0;
        uint32_t now = millis();
        if (now - lastFlash > 60) {
            leds::signal(leds::Channel::Ble, leds::Event::Rx);
            lastFlash = now;
        }
        if (!g_mutex) return;
        if (xSemaphoreTake(g_mutex, 0) != pdTRUE) return; // drop if contended

        const uint8_t* rawMac = dev->getAddress().getBase()->val;
        uint8_t mac[6];
        // NimBLE stores MAC little-endian; reverse to the usual big-endian
        // display order.
        for (int i = 0; i < 6; ++i) mac[i] = rawMac[5 - i];

        int idx = findMac(mac);
        if (idx < 0) {
            if (g_count < MAX_RESULTS) {
                idx = g_count++;
                memcpy(g_results[idx].mac, mac, 6);
                g_results[idx].packetCount = 0;
            } else {
                xSemaphoreGive(g_mutex);
                return;
            }
        }
        auto& e = g_results[idx];
        e.name = dev->haveName() ? String(dev->getName().c_str()) : String();
        e.rssi = (int8_t)dev->getRSSI();
        e.lastSeenMs = millis();
        e.packetCount++;

        xSemaphoreGive(g_mutex);
    }
};

ScanCb g_cb;

} // namespace

bool startScan() {
    if (!radio::acquire(radio::Owner::Ble)) return false;
    if (g_running) return true;

    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();

    if (!NimBLEDevice::isInitialized()) {
        NimBLEDevice::init("ESP32DIV");
    }

    g_scan = NimBLEDevice::getScan();
    g_scan->setScanCallbacks(&g_cb, /*wantDuplicates=*/false);
    g_scan->setActiveScan(true);
    g_scan->setInterval(100);
    g_scan->setWindow(99);
    // duration=0 → continuous until stop() is called.
    bool ok = g_scan->start(0, false);
    g_running = ok;
    return ok;
}

void stop() {
    if (g_scan && g_running) g_scan->stop();
    g_running = false;
    radio::release(radio::Owner::Ble);
}

bool scanRunning() { return g_running; }

int resultCount() { return g_count; }

bool resultAt(int i, BleEntry& out) {
    if (i < 0 || i >= g_count) return false;
    if (!g_mutex) return false;
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    out = g_results[i];
    xSemaphoreGive(g_mutex);
    return true;
}

void clearResults() {
    if (!g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_count = 0;
    xSemaphoreGive(g_mutex);
}

} // namespace ble
} // namespace radio
