#pragma once

#include <Arduino.h>   // String
#include <stdint.h>

namespace storage {

enum class UsbMode : uint8_t {
    Standalone = 0,  // local UI owns device; USB is optional MSC/CDC/HID
    Bridge     = 1,  // local UI + host CDC radio protocol active
    Appliance  = 2,  // HackRF-style: no UI, all radios to host via USB
};

enum class ShieldProfile : uint8_t {
    Nrf24Triple  = 0, // 3× NRF24 populated, IR unused (default for v2 shield)
    Nrf24DualIr  = 1, // 2× NRF24 + IR; NRF24 #3 pins repurposed for IR
};

struct Settings {
    UsbMode       usbMode       = UsbMode::Standalone;
    ShieldProfile shield        = ShieldProfile::Nrf24Triple;
    uint8_t       brightness    = 200;   // 0..255
    bool          neopixel      = true;
    bool          autoWifiScan  = true;
    bool          autoBleScan   = true;
    uint16_t      touchXMin     = 300;
    uint16_t      touchXMax     = 3800;
    uint16_t      touchYMin     = 300;
    uint16_t      touchYMax     = 3800;
    // Orientation, detected from 4-corner calibration:
    //   swapXY:   raw X-axis actually drives screen Y (and vice versa)
    //   invertX:  screen-X decreases as the source axis increases
    //   invertY:  screen-Y decreases as the source axis increases
    // Defaults work for the most common ESP32-DIV v2 panel orientation.
    bool          touchSwapXY   = false;
    bool          touchInvertX  = true;
    bool          touchInvertY  = true;

    uint8_t       theme         = 0;     // 0=Dark 1=Light

    // Captive portal last-used SSID — persisted so reopening the screen
    // keeps the AP name you typed.
    String        portalSsid    = "Free WiFi";

    // Station-mode WiFi credentials. Used on boot to get NTP, and
    // anytime the user hits "Connect" on the Wi-Fi → Network screen.
    // Empty = no auto-connect.
    String        staSsid;
    String        staPassword;

    // Running count of consecutive failed boot-time auto-connects. At 3+
    // we stop auto-retrying so the device can't get stuck in a connect/
    // watchdog-reboot loop with bad credentials. Reset to 0 when the
    // user edits SSID/password or a connect succeeds.
    uint8_t       staFailCount  = 0;

    // Timezone offset + DST, in seconds from UTC. Default 0/0 = UTC.
    // No UI to set these yet; adjust via NVS or add a Settings toggle.
    int32_t       tzOffsetSec   = 0;
    int32_t       tzDstOffsetSec = 0;

    // ── Addressable LED configuration ─────────────────────────────────
    // 4 WS2812B2020s on the front of the main board. Each LED symbolizes
    // a radio interface (LED 0 = Wi-Fi, 1 = BLE, 2 = Sub-GHz, 3 = NRF24)
    // and flashes on RX/TX activity with user-picked colors.
    bool     ledEnabled     = true;
    uint8_t  ledPin         = 48;        // WS2812 data line; configurable
    uint8_t  ledBrightness  = 48;        // 0..255 on a 4-pixel chain
    uint32_t ledRxWifi      = 0x0000FF;  // blue RX default
    uint32_t ledTxWifi      = 0xFF0000;  // red  TX default
    uint32_t ledRxBle       = 0x0000FF;
    uint32_t ledTxBle       = 0xFF0000;
    uint32_t ledRxSubghz    = 0x0000FF;
    uint32_t ledTxSubghz    = 0xFF0000;
    uint32_t ledRxNrf24     = 0x0000FF;
    uint32_t ledTxNrf24     = 0xFF0000;
};

// Load from NVS (or defaults if first boot). Safe to call once during setup.
void load();

// Persist current settings to NVS.
void save();

// Mutable reference to the live settings. Callers mutate + call save().
Settings& mut();

// Read-only access.
const Settings& get();

} // namespace storage
