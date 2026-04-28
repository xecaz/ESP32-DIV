#include "WifiCapture.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "RadioManager.h"

namespace radio {
namespace wifi_cap {

namespace {

// PCAP global header (24 bytes) — libpcap magic number indicating
// microsecond timestamps, snaplen 65535, link-type 105 (IEEE802.11).
struct __attribute__((packed)) PcapGlobalHdr {
    uint32_t magic       = 0xa1b2c3d4;
    uint16_t vMajor      = 2;
    uint16_t vMinor      = 4;
    int32_t  thiszone    = 0;
    uint32_t sigfigs     = 0;
    uint32_t snaplen     = 65535;
    uint32_t network     = 105; // LINKTYPE_IEEE802_11
};

struct __attribute__((packed)) PcapRecordHdr {
    uint32_t tsSec;
    uint32_t tsUsec;
    uint32_t inclLen;
    uint32_t origLen;
};

// Per-packet record pushed from the promiscuous ISR-like callback into a
// FreeRTOS queue for the writer task. We cap payload to 256 bytes — plenty
// for the common beacon/probe/deauth we care about, keeps memory bounded.
struct QueuedPkt {
    uint32_t tsSec, tsUsec;
    uint16_t len;
    uint8_t  payload[256];
};

QueueHandle_t g_queue = nullptr;
TaskHandle_t  g_task  = nullptr;
File          g_file;
String        g_filename;
bool          g_running = false;
bool          g_stop    = false;
uint32_t      g_pkts    = 0;
uint32_t      g_bytes   = 0;
uint32_t      g_hopMs   = 250;

// Rate history: one bucket per second, ring buffer.
uint16_t      g_rate[RATE_HISTORY_LEN] = {};
uint32_t      g_rateHeadMs   = 0;  // boundary (millis()) for the current bucket
uint32_t      g_rateHeadPkts = 0;  // g_pkts snapshot at boundary
int           g_rateHeadIdx  = 0;  // next slot to write
uint16_t      g_ratePeak     = 0;

void IRAM_ATTR promCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!g_queue) return;
    auto* pkt = (wifi_promiscuous_pkt_t*)buf;
    QueuedPkt q;
    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len > sizeof(q.payload)) len = sizeof(q.payload);
    memcpy(q.payload, pkt->payload, len);
    q.len = len;
    uint64_t us = esp_timer_get_time();
    q.tsSec  = (uint32_t)(us / 1000000ULL);
    q.tsUsec = (uint32_t)(us % 1000000ULL);
    xQueueSendFromISR(g_queue, &q, nullptr);
}

void writerTask(void*) {
    uint8_t channel = 1;
    uint32_t lastHopMs = 0;

    PcapGlobalHdr gh;
    g_file.write((uint8_t*)&gh, sizeof(gh));
    g_file.flush();
    g_bytes += sizeof(gh);

    QueuedPkt q;
    while (!g_stop) {
        int drained = 0;
        while (xQueueReceive(g_queue, &q, 0) == pdTRUE) {
            PcapRecordHdr rh{ q.tsSec, q.tsUsec, q.len, q.len };
            g_file.write((uint8_t*)&rh, sizeof(rh));
            g_file.write(q.payload, q.len);
            g_bytes += sizeof(rh) + q.len;
            g_pkts++;
            if (++drained >= 32) break;
        }
        if (drained) g_file.flush();

        uint32_t now = millis();
        if (now - lastHopMs >= g_hopMs) {
            channel = channel % 11 + 1;
            esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            lastHopMs = now;
        }

        // Rate bucket: every 1 s, record delta packets.
        if (now - g_rateHeadMs >= 1000) {
            uint32_t delta = g_pkts - g_rateHeadPkts;
            uint16_t rate  = delta > 0xFFFF ? 0xFFFF : (uint16_t)delta;
            g_rate[g_rateHeadIdx] = rate;
            g_rateHeadIdx = (g_rateHeadIdx + 1) % RATE_HISTORY_LEN;
            g_rateHeadPkts = g_pkts;
            g_rateHeadMs   = now;
            if (rate > g_ratePeak) g_ratePeak = rate;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
    g_file.close();
    g_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

bool start() {
    if (!radio::acquire(radio::Owner::Wifi)) return false;
    if (!SD.exists("/captures")) SD.mkdir("/captures");
    // Filename: ptm_YYYYMMDD_HHMMSS.pcap if the ESP32 has any notion of
    // wall-clock time (NTP-synced at some point), otherwise fall back to
    // ptm_<boot_seconds>.pcap so sequential captures still sort sanely.
    char name[64];
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (now > 1700000000) {
        strftime(name, sizeof(name), "/captures/ptm_%Y%m%d_%H%M%S.pcap", &tmv);
    } else {
        snprintf(name, sizeof(name), "/captures/ptm_boot%lus.pcap",
                 (unsigned long)(millis() / 1000));
    }
    g_file = SD.open(name, FILE_WRITE);
    if (!g_file) { radio::release(radio::Owner::Wifi); return false; }
    g_filename = name;

    if (!g_queue) g_queue = xQueueCreate(256, sizeof(QueuedPkt));

    // WiFi.mode(STA) forces esp_wifi into a known-good state — we can't
    // call esp_wifi_init(nullptr) ourselves, it just fails. After that
    // the exact sequence that works for Deauth Detect: set the rx cb,
    // install an explicit mgmt+data filter (default can drop data frames
    // on some IDF versions, which is why previous builds wrote only the
    // 24-byte PCAP header and nothing else), then enable promiscuous
    // and pin the channel.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    esp_wifi_set_promiscuous_rx_cb(&promCb);
    wifi_promiscuous_filter_t filt{};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
                     | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    g_pkts = g_bytes = 0;
    memset(g_rate, 0, sizeof(g_rate));
    g_rateHeadIdx  = 0;
    g_rateHeadPkts = 0;
    g_rateHeadMs   = millis();
    g_ratePeak     = 0;
    g_stop = false; g_running = true;
    xTaskCreatePinnedToCore(writerTask, "pcap_wr", 8192, nullptr,
                            /*prio=*/1, &g_task, /*coreId=*/1);
    return true;
}

void stop() {
    if (!g_running) return;
    g_stop = true;
    for (int i = 0; i < 50 && g_task; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    esp_wifi_set_promiscuous(false);
    WiFi.mode(WIFI_OFF);
    g_running = false;
    radio::release(radio::Owner::Wifi);
}

bool     running()          { return g_running; }
String   currentFilename()  { return g_running ? g_filename : String(); }
uint32_t packetsCaptured()  { return g_pkts; }
uint32_t bytesWritten()     { return g_bytes; }
void     setChannelHopMs(uint32_t ms) { g_hopMs = ms; }

void rateHistory(uint16_t out[RATE_HISTORY_LEN]) {
    // Unroll the ring into chronological order: oldest first, newest last.
    for (int i = 0; i < RATE_HISTORY_LEN; ++i) {
        int idx = (g_rateHeadIdx + i) % RATE_HISTORY_LEN;
        out[i] = g_rate[idx];
    }
}

uint16_t peakPacketsPerSec() { return g_ratePeak; }

} // namespace wifi_cap
} // namespace radio
