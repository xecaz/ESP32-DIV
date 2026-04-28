#include "Cc1101Driver.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <SD.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hw/Pins.h"
#include "../hw/Leds.h"
#include "RadioManager.h"

namespace radio {
namespace cc1101 {

namespace {
RCSwitch  g_mySwitch;
Capture   g_lastCapture{};
bool      g_haveCapture  = false;
bool      g_rx           = false;
bool      g_jam          = false;
TaskHandle_t g_rxTask    = nullptr;
TaskHandle_t g_jamTask   = nullptr;
bool      g_stopRx       = false;
bool      g_stopJam      = false;

void initCc1101(uint32_t freqHz) {
    ELECHOUSE_cc1101.setSpiPin(pins::SPI_SCLK, pins::SPI_MISO,
                               pins::SPI_MOSI, pins::CC1101_CS);
    ELECHOUSE_cc1101.Init();
    // setGDO() routes the demodulated data onto GDO0/GDO2 — required so
    // RCSwitch's ISR on the GPIO sees anything at all.
    ELECHOUSE_cc1101.setGDO(pins::CC1101_GDO0, pins::CC1101_GDO2);
    // CC1101 defaults to 2-FSK. Typical keyfobs / doorbells / weather
    // stations on 433.92 are OOK, so FSK demodulation receives noise and
    // RCSwitch never triggers. Explicit ASK/OOK (modulation mode 2)
    // fixes that. RxBW 200 kHz is wide enough for HackRF's slightly
    // drifty transmit and most consumer remotes. syncMode=0 = no sync
    // word (async serial bitstream — what RCSwitch expects).
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setRxBW(200);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setMHZ(freqHz / 1000000.0f);
}

void rxTask(void*) {
    initCc1101(g_lastCapture.freqHz);
    ELECHOUSE_cc1101.SetRx();
    g_mySwitch.disableReceive();
    g_mySwitch.enableReceive(pins::CC1101_GDO2);

    while (!g_stopRx) {
        if (g_mySwitch.available()) {
            g_lastCapture.value     = (uint32_t)g_mySwitch.getReceivedValue();
            g_lastCapture.bitLength = g_mySwitch.getReceivedBitlength();
            g_lastCapture.protocol  = g_mySwitch.getReceivedProtocol();
            if (g_lastCapture.value) {
                g_haveCapture = true;
                leds::signal(leds::Channel::Subghz, leds::Event::Rx);
            }
            g_mySwitch.resetAvailable();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    g_mySwitch.disableReceive();
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
    g_rxTask = nullptr;
    vTaskDelete(nullptr);
}

void jamTask(void*) {
    initCc1101(g_lastCapture.freqHz);
    ELECHOUSE_cc1101.SetTx();
    // Spew continuous carrier: write TXFIFO with alternating bytes.
    while (!g_stopJam) {
        for (int i = 0; i < 64; ++i) {
            ELECHOUSE_cc1101.SpiWriteReg(0x3F /*TXFIFO*/, 0xAA);
        }
        delayMicroseconds(50);
    }
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
    g_jamTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

bool startRx(uint32_t freqHz) {
    if (!radio::acquire(radio::Owner::Cc1101)) return false;
    g_lastCapture.freqHz = freqHz;
    g_haveCapture = false;
    g_stopRx      = false;
    g_rx          = true;
    xTaskCreatePinnedToCore(rxTask, "cc_rx", 4096, nullptr, 1, &g_rxTask, 1);
    return true;
}

void stopRx() {
    if (!g_rx) return;
    g_stopRx = true;
    for (int i = 0; i < 30 && g_rxTask; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    g_rx = false;
    radio::release(radio::Owner::Cc1101);
}

bool rxRunning()       { return g_rx; }
bool rxHaveCapture()   { return g_haveCapture; }
bool rxLatestCapture(Capture& out) {
    if (!g_haveCapture) return false;
    out = g_lastCapture;
    return true;
}

bool txCapture(const Capture& c) {
    if (!radio::acquire(radio::Owner::Cc1101)) return false;
    initCc1101(c.freqHz);
    ELECHOUSE_cc1101.SetTx();
    RCSwitch s;
    s.enableTransmit(pins::CC1101_GDO0);
    s.setProtocol(c.protocol ? c.protocol : 1);
    s.setPulseLength(350); // reasonable default; most remotes land near here
    leds::signal(leds::Channel::Subghz, leds::Event::Tx);
    s.send(c.value, c.bitLength ? c.bitLength : 24);
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
    radio::release(radio::Owner::Cc1101);
    return true;
}

bool startJammer(uint32_t freqHz) {
    if (!radio::acquire(radio::Owner::Cc1101)) return false;
    g_lastCapture.freqHz = freqHz;
    g_stopJam = false;
    g_jam     = true;
    xTaskCreatePinnedToCore(jamTask, "cc_jam", 4096, nullptr, 1, &g_jamTask, 1);
    return true;
}

void stopJammer() {
    if (!g_jam) return;
    g_stopJam = true;
    for (int i = 0; i < 30 && g_jamTask; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    g_jam = false;
    radio::release(radio::Owner::Cc1101);
}

bool jammerRunning() { return g_jam; }

// ── SD-backed saved profiles ───────────────────────────────────────────
namespace {
constexpr uint32_t SGPF_MAGIC = 0x46504753;  // 'SGPF'
constexpr const char* SGPF_DIR = "/subghz";

String slug(const char* label) {
    String out;
    bool lastUnderscore = false;
    for (const char* p = label; p && *p; ++p) {
        char c = *p;
        bool keep = (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='.';
        if (keep) { out += c; lastUnderscore = false; }
        else if (!lastUnderscore && out.length()) { out += '_'; lastUnderscore = true; }
        if (out.length() >= 24) break;
    }
    while (out.length() && out[out.length()-1] == '_') out.remove(out.length()-1);
    return out;
}

String nextAuto() {
    int next = 0;
    File d = SD.open(SGPF_DIR);
    if (d) {
        while (File f = d.openNextFile()) {
            int n = 0;
            String name = f.name();
            int sl = name.lastIndexOf('/');
            if (sl >= 0) name = name.substring(sl+1);
            if (sscanf(name.c_str(), "sg_%d.bin", &n) == 1 && n >= next) next = n+1;
            f.close();
        }
        d.close();
    }
    char buf[32]; snprintf(buf, sizeof(buf), "sg_%04d.bin", next);
    return String(buf);
}

String pickName(const char* label) {
    String s = slug(label);
    if (!s.length()) return nextAuto();
    String base = s + ".bin";
    if (!SD.exists((String(SGPF_DIR) + "/" + base).c_str())) return base;
    for (int n = 2; n < 1000; ++n) {
        String c = s + "_" + String(n) + ".bin";
        if (!SD.exists((String(SGPF_DIR) + "/" + c).c_str())) return c;
    }
    return nextAuto();
}
} // namespace

String saveCaptureToSd(const Capture& c, const char* label) {
    if (!SD.exists(SGPF_DIR)) SD.mkdir(SGPF_DIR);
    String base = pickName(label);
    File f = SD.open((String(SGPF_DIR) + "/" + base).c_str(), FILE_WRITE);
    if (!f) return String();
    SavedHeader h{};
    h.magic      = SGPF_MAGIC;
    h.version    = 1;
    h.frequency  = c.freqHz;
    h.value      = c.value;
    h.bitLength  = c.bitLength;
    h.protocol   = c.protocol;
    if (label) strncpy(h.name, label, sizeof(h.name) - 1);
    f.write((uint8_t*)&h, sizeof(h));
    f.close();
    return base;
}

int listSaved(String* out, int max) {
    if (!SD.exists(SGPF_DIR)) return 0;
    File d = SD.open(SGPF_DIR);
    if (!d) return 0;
    int n = 0;
    while (File f = d.openNextFile()) {
        if (!f.isDirectory()) {
            String name = f.name();
            int sl = name.lastIndexOf('/');
            if (sl >= 0) name = name.substring(sl+1);
            if (name.endsWith(".bin")) {
                if (out && n < max) out[n] = name;
                ++n;
            }
        }
        f.close();
    }
    d.close();
    return n;
}

bool loadCapture(const String& basename, Capture& out) {
    File f = SD.open((String(SGPF_DIR) + "/" + basename).c_str(), FILE_READ);
    if (!f) return false;
    SavedHeader h{};
    if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) || h.magic != SGPF_MAGIC) {
        f.close(); return false;
    }
    out.freqHz    = h.frequency;
    out.value     = h.value;
    out.bitLength = h.bitLength;
    out.protocol  = h.protocol;
    f.close();
    return true;
}

bool deleteSaved(const String& basename) {
    return SD.remove((String(SGPF_DIR) + "/" + basename).c_str());
}

} // namespace cc1101
} // namespace radio
