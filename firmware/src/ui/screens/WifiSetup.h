#pragma once

#include <Arduino.h>
#include "../Screen.h"

namespace ui {

// Store station-mode Wi-Fi credentials + one-tap Connect-and-NTP-sync.
// Values persist in NVS so the device can auto-connect on boot.
class WifiSetupScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    String   ssid_;
    String   password_;
    uint32_t lastRenderMs_ = 0;

    // "Pick from scan" overlay. When true we render the scanned AP list
    // instead of the setup fields; tap a row to copy its SSID into ssid_.
    bool pickerOpen_    = false;
    bool scanStarted_   = false;
    int  pickCursor_    = 0;
    int  pickScroll_    = 0;
    int  lastScanCount_ = -1;

    // "show password" toggle — flips the masked dots into cleartext on
    // the setup field. Also applied when opening the keyboard so the
    // user can see what they're typing.
    bool pwVisible_     = false;

    void doConnect();
    void openPicker();
    void closePicker();
};

// Connect runs on its own task so the ~10 s WiFi.begin timeout never
// blocks the UI. State lives at module scope so the task keeps running
// (and publishing results) even if the user navigates away mid-connect.
namespace wifi_setup_task {
enum class State : uint8_t { Idle, Busy, Synced, FailedConn, FailedNtp };

void   start(const String& ssid, const String& password);
State  state();
String detail();
bool   busy();
} // namespace wifi_setup_task

} // namespace ui
