#include "IrDriver.h"

#include <IRrecv.h>
#include <IRsend.h>
#include <IRremoteESP8266.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hw/Pins.h"
#include "RadioManager.h"

namespace radio {
namespace ir {

namespace {
constexpr uint16_t CAPTURE_BUF = 1024;
constexpr uint8_t  TIMEOUT_MS  = 50;

IRrecv*      g_recv = nullptr;
IRsend*      g_send = nullptr;
TaskHandle_t g_rxTask = nullptr;
bool         g_stopRx = false;
bool         g_running = false;

IrCapture g_capture{};
bool      g_have = false;

void rxTask(void*) {
    if (!g_recv) g_recv = new IRrecv(pins::IR_RX, CAPTURE_BUF, TIMEOUT_MS, true);
    g_recv->enableIRIn();

    decode_results results;
    while (!g_stopRx) {
        if (g_recv->decode(&results)) {
            g_capture.decodedValue = (uint32_t)results.value;
            g_capture.bits         = results.bits;
            g_capture.protocol     = (uint8_t)results.decode_type;
            g_capture.khz          = 38;
            uint16_t rawLen = (uint16_t)results.rawlen;
            if (rawLen > sizeof(g_capture.rawBuf)/sizeof(g_capture.rawBuf[0]))
                rawLen = sizeof(g_capture.rawBuf)/sizeof(g_capture.rawBuf[0]);
            for (uint16_t i = 0; i < rawLen; ++i)
                g_capture.rawBuf[i] = results.rawbuf[i];
            g_capture.rawLen = rawLen;
            g_have = true;
            g_recv->resume();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    g_recv->disableIRIn();
    g_rxTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

bool startRx() {
    if (!radio::acquire(radio::Owner::Ir)) return false;
    g_have = false;
    g_stopRx = false;
    g_running = true;
    xTaskCreatePinnedToCore(rxTask, "ir_rx", 4096, nullptr, 1, &g_rxTask, 1);
    return true;
}

void stopRx() {
    if (!g_running) return;
    g_stopRx = true;
    for (int i = 0; i < 30 && g_rxTask; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    g_running = false;
    radio::release(radio::Owner::Ir);
}

bool rxRunning()  { return g_running; }
bool haveCapture(){ return g_have; }
bool getCapture(IrCapture& out) {
    if (!g_have) return false;
    out = g_capture;
    return true;
}
void clearCapture() { g_have = false; }

namespace {
constexpr uint32_t MAGIC = 0x46505249; // 'IRPF'
constexpr const char* DIR = "/ir";

// Make a label filesystem-safe: strip control chars, replace anything
// that isn't alnum/dash/underscore with an underscore, collapse runs,
// trim trailing junk. Returns empty if the label was entirely unusable.
String slugify(const char* label) {
    String out;
    out.reserve(24);
    bool lastUnderscore = false;
    for (const char* p = label; p && *p; ++p) {
        char c = *p;
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (keep) {
            out += c;
            lastUnderscore = false;
        } else if (!lastUnderscore && out.length()) {
            out += '_';
            lastUnderscore = true;
        }
        if (out.length() >= 24) break;
    }
    while (out.length() && out[out.length() - 1] == '_') out.remove(out.length() - 1);
    return out;
}

// Auto-increment fallback for unnamed/empty/duplicate saves.
String nextFreeAutoName() {
    if (!SD.exists(DIR)) SD.mkdir(DIR);
    int next = 0;
    File d = SD.open(DIR);
    if (d) {
        while (File f = d.openNextFile()) {
            int n = 0;
            String name = f.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (sscanf(name.c_str(), "ir_%d.bin", &n) == 1 && n >= next) next = n + 1;
            f.close();
        }
        d.close();
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "ir_%04d.bin", next);
    return String(buf);
}

// Pick a filename derived from the user's label. If the label collapses
// to empty, fall back to ir_NNNN.bin. If the label is taken, append _2,
// _3, …
String pickFilename(const char* label) {
    String slug = slugify(label);
    if (!slug.length()) return nextFreeAutoName();
    String base = slug + ".bin";
    String path = String(DIR) + "/" + base;
    if (!SD.exists(path.c_str())) return base;
    for (int n = 2; n < 1000; ++n) {
        String candidate = slug + "_" + String(n) + ".bin";
        path = String(DIR) + "/" + candidate;
        if (!SD.exists(path.c_str())) return candidate;
    }
    return nextFreeAutoName();  // ran out of suffixes — last resort
}
} // namespace

String saveCapture(const IrCapture& c, const char* label) {
    if (!SD.exists(DIR)) SD.mkdir(DIR);
    // Filename tracks the user's label (slugified); duplicates get _2, _3…
    // Empty labels fall back to auto-numbered ir_NNNN.bin.
    String base = pickFilename(label);
    String path = String(DIR) + "/" + base;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return String();

    SavedHeader h{};
    h.magic      = MAGIC;
    h.version    = 1;
    h.khz        = c.khz ? c.khz : 38;
    h.rawLen     = c.rawLen;
    h.decodeType = c.protocol;
    h.bits       = c.bits;
    h.value      = c.decodedValue;
    if (label) strncpy(h.name, label, sizeof(h.name) - 1);

    f.write((uint8_t*)&h, sizeof(h));
    f.write((uint8_t*)c.rawBuf, c.rawLen * sizeof(uint16_t));
    f.close();
    return base;
}

int listSaved(String* out, int max) {
    if (!SD.exists(DIR)) return 0;
    File d = SD.open(DIR);
    if (!d) return 0;
    int n = 0;
    while (File f = d.openNextFile()) {
        if (!f.isDirectory()) {
            String name = f.name();
            // SD library sometimes returns full path — normalize to basename.
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.endsWith(".bin") && out && n < max) out[n] = name;
            if (name.endsWith(".bin")) ++n;
        }
        f.close();
    }
    d.close();
    return n;
}

bool loadCapture(const String& basename, IrCapture& c) {
    String path = String(DIR) + "/" + basename;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    SavedHeader h{};
    if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) || h.magic != MAGIC) {
        f.close(); return false;
    }
    c.khz          = h.khz;
    c.protocol     = h.decodeType;
    c.bits         = h.bits;
    c.decodedValue = (uint32_t)h.value;
    c.rawLen       = h.rawLen;
    if (c.rawLen > sizeof(c.rawBuf) / sizeof(c.rawBuf[0]))
        c.rawLen = sizeof(c.rawBuf) / sizeof(c.rawBuf[0]);
    f.read((uint8_t*)c.rawBuf, c.rawLen * sizeof(uint16_t));
    f.close();
    return true;
}

bool deleteSaved(const String& basename) {
    String path = String(DIR) + "/" + basename;
    return SD.remove(path.c_str());
}

bool txCapture(const IrCapture& c) {
    if (!radio::acquire(radio::Owner::Ir)) return false;
    if (!g_send) g_send = new IRsend(pins::IR_TX);
    g_send->begin();
    if (c.protocol != UNKNOWN && c.decodedValue) {
        g_send->send((decode_type_t)c.protocol, c.decodedValue, c.bits);
    } else {
        // Unknown / raw — replay the captured timings.
        // Convert IRremote's 50us ticks back to microseconds.
        static uint16_t raw[200];
        uint16_t n = c.rawLen > 200 ? 200 : c.rawLen;
        for (uint16_t i = 0; i < n; ++i) raw[i] = c.rawBuf[i] * 50;
        g_send->sendRaw(raw, n, c.khz ? c.khz : 38);
    }
    radio::release(radio::Owner::Ir);
    return true;
}

} // namespace ir
} // namespace radio
