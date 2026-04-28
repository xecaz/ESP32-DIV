#include "Settings.h"

#include <Preferences.h>

namespace storage {

namespace {
Settings         g_settings;
constexpr const char* NS = "esp32div";
}

Settings&       mut() { return g_settings; }
const Settings& get() { return g_settings; }

// Settings schema version. Bump when a default changes so existing devices
// get the new value on next boot. Migration logic below reads the stored
// version and patches whatever fields the bump covers.
constexpr uint8_t SETTINGS_VERSION = 2;

void load() {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) return;

    g_settings.usbMode      = static_cast<UsbMode>(p.getUChar("usbMode", static_cast<uint8_t>(UsbMode::Standalone)));
    g_settings.shield       = static_cast<ShieldProfile>(p.getUChar("shield", static_cast<uint8_t>(ShieldProfile::Nrf24Triple)));
    g_settings.brightness   = p.getUChar("bright", 200);
    g_settings.neopixel     = p.getBool("npx", true);
    g_settings.autoWifiScan = p.getBool("awifi", true);
    g_settings.autoBleScan  = p.getBool("able", true);
    g_settings.touchXMin    = p.getUShort("tXMin", 300);
    g_settings.touchXMax    = p.getUShort("tXMax", 3800);
    g_settings.touchYMin    = p.getUShort("tYMin", 300);
    g_settings.touchYMax    = p.getUShort("tYMax", 3800);
    g_settings.touchSwapXY  = p.getBool("tSwap", false);
    g_settings.touchInvertX = p.getBool("tInvX", true);
    g_settings.touchInvertY = p.getBool("tInvY", true);
    g_settings.theme        = p.getUChar("theme", 0);
    g_settings.portalSsid   = p.getString("pssid", "Free WiFi");
    g_settings.staSsid      = p.getString("staSsid", "");
    g_settings.staPassword  = p.getString("staPw", "");
    g_settings.staFailCount = p.getUChar("staFails", 0);
    g_settings.tzOffsetSec  = p.getInt("tzOff", 0);
    g_settings.tzDstOffsetSec = p.getInt("tzDst", 0);
    g_settings.ledEnabled   = p.getBool  ("ledOn", true);
    // Data line to D1.DI is GPIO1 (via R33 1kΩ series) per the board
    // schematic. Stock firmware hard-coded this; we expose it as a setting
    // in case a future board revision relocates it.
    g_settings.ledPin       = p.getUChar ("ledPin", 1);
    g_settings.ledBrightness= p.getUChar ("ledBri", 48);
    g_settings.ledRxWifi    = p.getUInt  ("ledRxWf", 0x0000FF);
    g_settings.ledTxWifi    = p.getUInt  ("ledTxWf", 0xFF0000);
    g_settings.ledRxBle     = p.getUInt  ("ledRxBl", 0x0000FF);
    g_settings.ledTxBle     = p.getUInt  ("ledTxBl", 0xFF0000);
    g_settings.ledRxSubghz  = p.getUInt  ("ledRxSg", 0x0000FF);
    g_settings.ledTxSubghz  = p.getUInt  ("ledTxSg", 0xFF0000);
    g_settings.ledRxNrf24   = p.getUInt  ("ledRxNf", 0x0000FF);
    g_settings.ledTxNrf24   = p.getUInt  ("ledTxNf", 0xFF0000);

    uint8_t storedVer = p.getUChar("ver", 0);
    p.end();

    // v0→v2 migration: older firmware wrote ledPin=48 (a guess, wrong) and
    // ledBrightness=48 (dim). Reset both to the correct defaults and
    // persist so every boot after this one uses the new values. Any user
    // who has already customised these in the LED Settings screen has
    // ver=2 so we won't stomp on their values.
    if (storedVer < 2) {
        g_settings.ledPin        = 1;
        g_settings.ledBrightness = 48;
        save();  // rewrites all fields + bumps ver to SETTINGS_VERSION
    }

    // Sanity clamp: if NVS was corrupted (brightness=0 → black screen,
    // or touch bounds inverted/too tight), fall back to defaults so the
    // device always boots into a usable state. Re-running Touch Calibrate
    // or the Settings screen will overwrite these.
    if (g_settings.brightness < 20) g_settings.brightness = 200;
    if (g_settings.touchXMax <= g_settings.touchXMin ||
        (int)g_settings.touchXMax - (int)g_settings.touchXMin < 500) {
        g_settings.touchXMin = 300;
        g_settings.touchXMax = 3800;
    }
    if (g_settings.touchYMax <= g_settings.touchYMin ||
        (int)g_settings.touchYMax - (int)g_settings.touchYMin < 500) {
        g_settings.touchYMin = 300;
        g_settings.touchYMax = 3800;
    }
}

void save() {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) return;

    p.putUChar("usbMode",  static_cast<uint8_t>(g_settings.usbMode));
    p.putUChar("shield",   static_cast<uint8_t>(g_settings.shield));
    p.putUChar("bright",   g_settings.brightness);
    p.putBool ("npx",      g_settings.neopixel);
    p.putBool ("awifi",    g_settings.autoWifiScan);
    p.putBool ("able",     g_settings.autoBleScan);
    p.putUShort("tXMin",   g_settings.touchXMin);
    p.putUShort("tXMax",   g_settings.touchXMax);
    p.putUShort("tYMin",   g_settings.touchYMin);
    p.putUShort("tYMax",   g_settings.touchYMax);
    p.putBool  ("tSwap",   g_settings.touchSwapXY);
    p.putBool  ("tInvX",   g_settings.touchInvertX);
    p.putBool  ("tInvY",   g_settings.touchInvertY);
    p.putUChar ("theme",   g_settings.theme);
    p.putString("pssid",   g_settings.portalSsid);
    p.putString("staSsid", g_settings.staSsid);
    p.putString("staPw",   g_settings.staPassword);
    p.putUChar ("staFails",g_settings.staFailCount);
    p.putInt   ("tzOff",   g_settings.tzOffsetSec);
    p.putInt   ("tzDst",   g_settings.tzDstOffsetSec);
    p.putBool  ("ledOn",   g_settings.ledEnabled);
    p.putUChar ("ledPin",  g_settings.ledPin);
    p.putUChar ("ledBri",  g_settings.ledBrightness);
    p.putUInt  ("ledRxWf", g_settings.ledRxWifi);
    p.putUInt  ("ledTxWf", g_settings.ledTxWifi);
    p.putUInt  ("ledRxBl", g_settings.ledRxBle);
    p.putUInt  ("ledTxBl", g_settings.ledTxBle);
    p.putUInt  ("ledRxSg", g_settings.ledRxSubghz);
    p.putUInt  ("ledTxSg", g_settings.ledTxSubghz);
    p.putUInt  ("ledRxNf", g_settings.ledRxNrf24);
    p.putUInt  ("ledTxNf", g_settings.ledTxNrf24);

    p.putUChar ("ver",     SETTINGS_VERSION);
    p.end();
}

} // namespace storage
