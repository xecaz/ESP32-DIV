// CTRL//VOID firmware (running on ESP32-DIV hardware) — entrypoint.
// See /home/xecaz/.claude/plans/in-this-directory-you-zazzy-lark.md for the plan.

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

#include "hw/Board.h"
#include "hw/Leds.h"
#include "hw/Pins.h"
#include "ui/assets/Splash.h"
#include <TFT_eSPI.h>
#include "input/InputTask.h"
#include "radio/RadioManager.h"
#include "radio/WifiDriver.h"
#include "storage/Settings.h"
#include "ui/Theme.h"
#include "ui/UiTask.h"
#include "ui/screens/Appliance.h"
#include "ui/screens/MainMenu.h"
#include "usb/UsbComposite.h"

namespace {

// Seed the system clock from the firmware build timestamp (__DATE__ /
// __TIME__) so file names like ptm_<date>_<time>.pcap have a plausible
// value without an RTC or NTP sync. Each fresh build pushes the clock
// forward, so as long as you flash recent firmware the year/month will
// be correct; the time-of-day will drift from real life by however
// long the device has been running since boot.
void seedClockFromBuildTime() {
    if (time(nullptr) > 1700000000) return;  // already set (NTP?)

    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* d = __DATE__;  // "Mmm DD YYYY"
    int mon = (int)((strstr(months, d) - months) / 3);
    if (mon < 0 || mon > 11) mon = 0;
    int day  = atoi(d + 4);
    int year = atoi(d + 7);
    int hh = atoi(__TIME__);      // "HH:MM:SS"
    int mm = atoi(__TIME__ + 3);
    int ss = atoi(__TIME__ + 6);

    struct tm tmv{};
    tmv.tm_year = year - 1900;
    tmv.tm_mon  = mon;
    tmv.tm_mday = day;
    tmv.tm_hour = hh;
    tmv.tm_min  = mm;
    tmv.tm_sec  = ss;

    struct timeval tv { mktime(&tmv), 0 };
    settimeofday(&tv, nullptr);
}

// Background NTP sync: if WiFi station creds are set and the system clock
// is still at its build-time seed, try once to connect + sync. Runs on
// its own low-priority task so boot is never blocked, and releases the
// WiFi radio when done so other features can take it.
//
// Failure guard: after 3 consecutive failed boot connects, we skip the
// auto-attempt entirely. Without this, bad credentials (e.g. user typed
// the wrong password and rebooted) would make every boot spend ~10 s
// blocking on WiFi.begin; the device appeared to "freeze at its logo"
// because the UI task couldn't get the WiFi mutex back in time for user
// input to feel responsive. User can re-run Wi-Fi Setup to reset it.
void ntpTask(void*) {
    const auto& s = storage::get();
    if (s.staFailCount >= 3) { vTaskDelete(nullptr); return; }
    if (!s.staSsid.length()) { vTaskDelete(nullptr); return; }
    if (time(nullptr) >= 1700000000) { vTaskDelete(nullptr); return; } // already set

    bool connected = radio::wifi::connectStation(s.staSsid, s.staPassword, 10000);
    auto& mut = storage::mut();
    if (connected) {
        mut.staFailCount = 0;
        radio::wifi::ntpSync(8000);
        radio::wifi::disconnectStation();
    } else if (mut.staFailCount < 255) {
        mut.staFailCount++;
    }
    storage::save();
    vTaskDelete(nullptr);
}

const char* usbModeName(storage::UsbMode m) {
    switch (m) {
        case storage::UsbMode::Standalone: return "STANDALONE";
        case storage::UsbMode::Bridge:     return "BRIDGE";
        case storage::UsbMode::Appliance:  return "APPLIANCE";
    }
    return "?";
}

} // namespace

void setup() {
    // ARDUINO_USB_CDC_ON_BOOT is 0, so `Serial` is HardwareSerial(0) on
    // UART0 (the CP2102 chip / /dev/ttyUSB0). Native USB-OTG CDC is left
    // off entirely — running it without an actual host on the OTG port
    // appeared to interact badly with the I²C peripheral.
    Serial.begin(115200);

    // Log why we rebooted. Most reboots on this board are caused by the
    // CP2102's DTR line glitching EN on USB plug/unplug (= EXT reset) —
    // a hardware quirk, not our bug. POWERON means true power cycle
    // (IP5306 woke up). PANIC/WDT mean actual firmware issues.
    esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("[boot] reset_reason=%d ", (int)rr);
    switch (rr) {
        case ESP_RST_POWERON:   Serial.println("(POWERON)"); break;
        case ESP_RST_EXT:       Serial.println("(EXT-reset from CP2102/USB plug)"); break;
        case ESP_RST_BROWNOUT:  Serial.println("(BROWNOUT — power sag)"); break;
        case ESP_RST_WDT:       Serial.println("(TASK-WDT)"); break;
        case ESP_RST_PANIC:     Serial.println("(PANIC — check coredump)"); break;
        case ESP_RST_SW:        Serial.println("(SW reset)"); break;
        default:                Serial.println("(other)"); break;
    }

    board::init();
    storage::load();
    ui::theme::apply();
    radio::init();
    seedClockFromBuildTime();
    board::setBacklight(storage::get().brightness);

    // Boot splash: centered on the 240x320 display. Held for ~1.5 s
    // while the rest of setup (SD mount attempt, radio init, USB)
    // completes. setBacklight(0) during pushImage hides a brief flicker
    // as the block transfers; we only restore brightness after show.
    board::tft.fillScreen(TFT_BLACK);
    const int sx = (240 - SPLASH_CTRLVOID_W) / 2;
    const int sy = (320 - SPLASH_CTRLVOID_H) / 2;
    board::tft.pushImage(sx, sy, SPLASH_CTRLVOID_W, SPLASH_CTRLVOID_H,
                         SPLASH_CTRLVOID);
    board::tft.setTextFont(2);
    board::tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    const char* tag = "booting...";
    int tw = board::tft.textWidth(tag);
    board::tft.setCursor((240 - tw) / 2, sy + SPLASH_CTRLVOID_H + 12);
    board::tft.print(tag);

    board::startStorageTask();
    leds::init();                // 4× WS2812 front-panel chain

    input::start();
    ui::start();
    // Hold the splash visible briefly after all subsystems are up so the
    // user actually gets to see it — otherwise MainMenu::onEnter wipes
    // the logo milliseconds after pushImage() finishes.
    delay(1200);

    // Branch on USB mode. Appliance boots straight into the static status
    // card with no menu — only the SELECT-hold escape gesture works.
    // Bridge is a soft variant of Standalone (M9 will turn on the extra
    // CDC radio-protocol channel); for now it boots like Standalone.
    //
    // Bisect aid for the "UI got laggy after M8" report: define
    // -DDISABLE_M8 in platformio.ini and the firmware will skip the
    // appliance path entirely (always pushes MainMenu, even if NVS still
    // has Appliance set). If the lag goes away with DISABLE_M8 then M8 is
    // really at fault; if not, M8 is a red herring and the cause is
    // something that landed alongside it.
#ifdef DISABLE_M8
    ui::push(new ui::MainMenu());
#else
    if (storage::get().usbMode == storage::UsbMode::Appliance) {
        ui::push(new ui::ApplianceScreen());
    } else {
        ui::push(new ui::MainMenu());
    }
#endif

    // Kick off background NTP — no-op if no creds stored.
    xTaskCreatePinnedToCore(ntpTask, "ntp", 4096, nullptr, 1, nullptr, 1);

    // Give the storage task ~2 s to mount the SD card before starting
    // the USB composite — MSC's descriptors need the card's sector count.
    // If SD isn't ready, USB still comes up with only the CDC console.
    for (int i = 0; i < 20 && !board::sdMounted(); ++i) delay(100);
    usb::start();

    Serial.printf("[boot] CTRL//VOID ready, usb=%s sd=%d\n",
                  usbModeName(storage::get().usbMode), (int)board::sdMounted());
}

void loop() {
    // UI + input run on their own FreeRTOS tasks; nothing to do here.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
