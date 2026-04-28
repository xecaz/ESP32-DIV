#pragma once

#include <Arduino.h>   // String
#include "../Screen.h"

namespace ui {

// Stage 1: scan for APs, pick one. Pushes DeauthClientScreen on Select.
class DeauthApScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;
private:
    int cursor_    = 0;
    int scrollTop_ = 0;
    int lastCount_ = -1;
};

// Stage 2: passive-listen for clients of a chosen AP, let user choose "all"
// or a specific client, push DeauthActiveScreen on Select.
class DeauthClientScreen : public Screen {
public:
    DeauthClientScreen(const uint8_t bssid[6], uint8_t channel, const char* ssid);
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;
private:
    uint8_t  bssid_[6]{};
    uint8_t  channel_ = 1;
    String   ssid_;
    int      cursor_ = 0;     // 0 = "broadcast", 1..n = client index (shifted)
    int      scrollTop_ = 0;
    int      lastCount_ = -1;
};

// Stage 3: hammer deauth frames at the chosen target. LEFT stops + pops back.
class DeauthActiveScreen : public Screen {
public:
    DeauthActiveScreen(const String& label);
    void onEnter(TFT_eSPI& tft) override;
    void onExit(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;
private:
    String   label_;
    uint32_t startedMs_ = 0;
    uint32_t lastTx_    = 0;
};

} // namespace ui
