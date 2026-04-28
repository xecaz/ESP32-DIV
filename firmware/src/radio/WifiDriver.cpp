#include "WifiDriver.h"

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

#include "RadioManager.h"
#include "../hw/Leds.h"
#include "../storage/Settings.h"

namespace radio {
namespace wifi {

namespace {
bool g_scanInFlight = false;
bool g_haveResults  = false;
int  g_lastCount    = 0;
}

bool startScan() {
    if (!radio::acquire(radio::Owner::Wifi)) return false;
    if (g_scanInFlight) return true;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    // `async=true` returns immediately; we poll via scanComplete().
    int started = WiFi.scanNetworks(/*async=*/true,
                                    /*show_hidden=*/true,
                                    /*passive=*/false,
                                    /*max_ms_per_chan=*/200);
    g_scanInFlight = (started == WIFI_SCAN_RUNNING);
    g_haveResults  = false;
    return g_scanInFlight;
}

bool scanDone() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) {
        // still running or never started
        if (n == WIFI_SCAN_FAILED && !g_scanInFlight) return false;
        return false;
    }
    // n is the count of networks found
    if (g_scanInFlight) {
        g_scanInFlight = false;
        g_haveResults  = true;
        g_lastCount    = n;
        if (n > 0) leds::signal(leds::Channel::Wifi, leds::Event::Rx);
    }
    return g_haveResults;
}

bool scanRunning() { return g_scanInFlight; }

int resultCount() { return g_haveResults ? g_lastCount : 0; }

bool resultAt(int i, ScanEntry& out) {
    if (!g_haveResults || i < 0 || i >= g_lastCount) return false;
    out.ssid    = WiFi.SSID(i);
    out.rssi    = WiFi.RSSI(i);
    out.channel = WiFi.channel(i);
    out.encType = (uint8_t)WiFi.encryptionType(i);
    const uint8_t* bssid = WiFi.BSSID(i);
    if (bssid) memcpy(out.bssid, bssid, 6);
    else       memset(out.bssid, 0, 6);
    return true;
}

void stop() {
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    g_scanInFlight = false;
    g_haveResults  = false;
    g_lastCount    = 0;
    radio::release(radio::Owner::Wifi);
}

// ── Station mode + NTP ────────────────────────────────────────────────

namespace {
bool g_staConnected = false;
}

bool connectStation(const String& ssid, const String& password,
                    uint32_t timeoutMs) {
    if (!ssid.length()) return false;
    if (!radio::acquire(radio::Owner::Wifi)) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(),
               password.length() ? password.c_str() : nullptr);

    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline && WiFi.status() != WL_CONNECTED) {
        delay(100);
    }
    g_staConnected = (WiFi.status() == WL_CONNECTED);
    if (!g_staConnected) {
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        radio::release(radio::Owner::Wifi);
    }
    return g_staConnected;
}

bool   stationConnected() { return g_staConnected && WiFi.status() == WL_CONNECTED; }
String stationIp()        { return stationConnected() ? WiFi.localIP().toString() : String(); }

void disconnectStation() {
    if (!g_staConnected) return;
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    g_staConnected = false;
    radio::release(radio::Owner::Wifi);
}

bool ntpSync(uint32_t timeoutMs) {
    if (!stationConnected()) return false;
    auto& s = storage::get();
    configTime(s.tzOffsetSec, s.tzDstOffsetSec,
               "pool.ntp.org", "time.nist.gov", "time.google.com");
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        if (time(nullptr) > 1700000000) return true;
        delay(100);
    }
    return false;
}

} // namespace wifi
} // namespace radio
