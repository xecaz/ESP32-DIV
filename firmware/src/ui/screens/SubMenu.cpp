#include "SubMenu.h"

#include <TFT_eSPI.h>
#include <string.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "Keyboard.h"
#include "WifiScan.h"
#include "BleScan.h"
#include "BleSniffer.h"
#include "BleSpoofer.h"
#include "SourApple.h"
#include "BleJammer.h"
#include "NrfSpectrum.h"
#include "Protokill.h"
#include "WifiRawScreens.h"
#include "SubGhzReplay.h"
#include "IrScreens.h"
#include "CaptivePortalScreen.h"
#include "PacketMonitor.h"
#include "DeauthFlow.h"
#include "FileBrowser.h"
#include "UsbHidTest.h"
#include "DuckyUsb.h"
#include "TouchCal.h"
#include "WifiSetup.h"
#include "About.h"
#include "LedSettings.h"
#include "Brightness.h"
#include "ThemePick.h"
#include "UsbModePick.h"
#include "I2cHealth.h"

namespace ui {

namespace {

const char* const WIFI_ITEMS[]   = { "Packet Monitor", "Beacon Spam", "Deauth Detect",
                                     "Deauth Attack", "Scanner", "Captive Portal", nullptr };
const char* const BLE_ITEMS[]    = { "Scanner", "Sniffer", "Jammer", "Spoofer",
                                     "Sour Apple", "Ducky (BLE)", nullptr };
const char* const NRF_ITEMS[]    = { "Spectrum", "Protokill", nullptr };
const char* const SUBGHZ_ITEMS[] = { "Record", "Replay", "Jammer", nullptr };
const char* const IR_ITEMS[]     = { "Record", "Replay", nullptr };
const char* const TOOLS_ITEMS[]  = { "Ducky (USB)", "USB HID Test", "Storage",
                                     "I2C Health", "Diagnostics",
                                     "Keyboard Test", nullptr };
const char* const SETTINGS_ITEMS[] = { "Wi-Fi Network", "LEDs", "USB Mode",
                                       "Brightness", "Theme",
                                       "Shield Profile", "Touch Calibrate",
                                       nullptr };
const char* const ABOUT_ITEMS[]  = { "Version", "MAC", "About", "License", nullptr };

constexpr int ITEM_H = 24;
constexpr int LIST_Y = 42;

int countItems(const char* const* items) {
    int n = 0;
    if (!items) return 0;
    while (items[n]) ++n;
    return n;
}

} // namespace

SubMenu::SubMenu(const char* categoryName) : category_(categoryName) {
    resolveItems();
    itemCount_ = countItems(items_);
}

void SubMenu::resolveItems() {
    if      (!strcmp(category_, "Wi-Fi"))     items_ = WIFI_ITEMS;
    else if (!strcmp(category_, "Bluetooth")) items_ = BLE_ITEMS;
    else if (!strcmp(category_, "2.4 GHz"))   items_ = NRF_ITEMS;
    else if (!strcmp(category_, "Sub-GHz"))   items_ = SUBGHZ_ITEMS;
    else if (!strcmp(category_, "IR"))        items_ = IR_ITEMS;
    else if (!strcmp(category_, "Tools"))     items_ = TOOLS_ITEMS;
    else if (!strcmp(category_, "Settings"))  items_ = SETTINGS_ITEMS;
    else if (!strcmp(category_, "About"))     items_ = ABOUT_ITEMS;
}

void SubMenu::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);
    tft.fillRect(0, 0, 240, 30, p.headerBg);
    tft.setTextFont(4);
    tft.setTextColor(p.headerFg, p.headerBg);
    tft.setCursor(12, 4);
    tft.print(category_);

    lastCursor_ = -1;
    dirty();
}

bool SubMenu::onEvent(const input::Event& e) {
    using input::EventType;
    using input::Key;

    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    switch (e.key) {
        case Key::Up:
            if (itemCount_ == 0) return true;
            cursor_ = (cursor_ - 1 + itemCount_) % itemCount_;
            dirty();
            return true;
        case Key::Down:
            if (itemCount_ == 0) return true;
            cursor_ = (cursor_ + 1) % itemCount_;
            dirty();
            return true;
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Select:
        case Key::Right:
            if (e.type == EventType::KeyDown && itemCount_ > 0) {
                const char* item = items_[cursor_];
                // Tools → Storage = SD file browser.
                if (!strcmp(category_, "Tools") && !strcmp(item, "Storage")) {
                    push(new FileBrowserScreen("/"));
                }
                else if (!strcmp(category_, "Tools") && !strcmp(item, "USB HID Test")) {
                    push(new UsbHidTestScreen());
                }
                else if (!strcmp(category_, "Tools") && !strcmp(item, "Ducky (USB)")) {
                    push(new DuckyUsbScreen());
                }
                else if (!strcmp(category_, "Tools") && !strcmp(item, "I2C Health")) {
                    push(new I2cHealthScreen());
                }
                else if (!strcmp(category_, "Settings") &&
                         !strcmp(item, "Touch Calibrate")) {
                    push(new TouchCalScreen());
                }
                else if (!strcmp(category_, "Settings") &&
                         !strcmp(item, "Wi-Fi Network")) {
                    push(new WifiSetupScreen());
                }
                else if (!strcmp(category_, "Settings") &&
                         !strcmp(item, "LEDs")) {
                    push(new LedSettingsScreen());
                }
                else if (!strcmp(category_, "Settings") &&
                         !strcmp(item, "Brightness")) {
                    push(new BrightnessScreen());
                }
                else if (!strcmp(category_, "Settings") &&
                         !strcmp(item, "Theme")) {
                    push(new ThemePickScreen());
                }
                else if (!strcmp(category_, "Settings") &&
                         !strcmp(item, "USB Mode")) {
                    push(new UsbModePickScreen());
                }
                else if (!strcmp(category_, "About")) {
                    if      (!strcmp(item, "Version")) push(new AboutScreen(AboutScreen::Page::Version));
                    else if (!strcmp(item, "MAC"))     push(new AboutScreen(AboutScreen::Page::Mac));
                    else if (!strcmp(item, "About"))   push(new AboutScreen(AboutScreen::Page::Credits));
                    else if (!strcmp(item, "License")) push(new AboutScreen(AboutScreen::Page::License));
                }
                // M4 test hook.
                else if (!strcmp(item, "Keyboard Test")) {
                    push(new Keyboard("Enter wifi password:", "",
                                      [](const String* t) {
                        if (t) Serial.printf("[kbd] got: '%s'\n", t->c_str());
                        else    Serial.println("[kbd] canceled");
                    }, /*mask=*/false));
                }
                // M5/M6 Wi-Fi features.
                else if (!strcmp(category_, "Wi-Fi")) {
                    if      (!strcmp(item, "Scanner"))
                        push(new WifiScanScreen());
                    else if (!strcmp(item, "Deauth Detect"))
                        push(new WifiRawScreen(radio::wifi_raw::Mode::DeauthDetect,
                                               "Deauth Detect"));
                    else if (!strcmp(item, "Beacon Spam"))
                        push(new WifiRawScreen(radio::wifi_raw::Mode::BeaconSpam,
                                               "Beacon Spam"));
                    else if (!strcmp(item, "Deauth Attack"))
                        push(new DeauthApScreen());   // proper scan→pick→fire flow
                    else if (!strcmp(item, "Captive Portal"))
                        push(new CaptivePortalScreen());
                    else if (!strcmp(item, "Packet Monitor"))
                        push(new PacketMonitorScreen());
                }
                // M6 Bluetooth features
                else if (!strcmp(category_, "Bluetooth")) {
                    if      (!strcmp(item, "Scanner"))   push(new BleScanScreen());
                    else if (!strcmp(item, "Sniffer"))   push(new BleSnifferScreen());
                    else if (!strcmp(item, "Spoofer"))   push(new BleSpooferScreen());
                    else if (!strcmp(item, "Sour Apple"))push(new SourAppleScreen());
                    else if (!strcmp(item, "Jammer"))    push(new BleJammerScreen());
                    // "Ducky (BLE)" lands in a later commit.
                }
                // M6 2.4GHz features
                else if (!strcmp(category_, "2.4 GHz")) {
                    if      (!strcmp(item, "Spectrum"))   push(new NrfSpectrumScreen());
                    else if (!strcmp(item, "Protokill"))  push(new ProtokillScreen());
                }
                // M6/M10 Sub-GHz features (CC1101).
                else if (!strcmp(category_, "Sub-GHz")) {
                    if      (!strcmp(item, "Record")) push(new SubGhzReplayScreen());
                    else if (!strcmp(item, "Replay")) push(new SubGhzProfilesScreen());
                    else if (!strcmp(item, "Jammer")) push(new SubGhzJammerScreen());
                }
                // M6 IR features.
                else if (!strcmp(category_, "IR")) {
                    if      (!strcmp(item, "Record")) push(new IrRecordScreen());
                    else if (!strcmp(item, "Replay")) push(new IrProfilesScreen());
                }
                // More M6 features land here one-by-one.
            }
            dirty();
            return true;
        default:
            return false;
    }
}

void SubMenu::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.setTextFont(2);
    if (itemCount_ == 0) {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(16, LIST_Y);
        tft.print("(no items)");
        tft.setCursor(16, LIST_Y + 20);
        tft.print("< back");
        return;
    }

    const bool fullPaint = (lastCursor_ < 0);

    for (int i = 0; i < itemCount_; ++i) {
        if (!fullPaint && i != cursor_ && i != lastCursor_) continue;
        int y = LIST_Y + i * ITEM_H;
        bool sel = (i == cursor_);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.text;
        tft.fillRect(0, y, 240, ITEM_H - 2, bg);
        tft.setTextColor(fg, bg);
        tft.setCursor(16, y + 4);
        tft.print(items_[i]);
        if (sel) {
            tft.setTextColor(p.accent, bg);
            tft.setCursor(220, y + 4);
            tft.print(">");
        }
    }

    if (fullPaint) {
        tft.setTextColor(p.textDim, p.bg);
        tft.fillRect(0, 302, 240, 18, p.bg);
        tft.setCursor(8, 304);
        tft.print("LEFT=back  SEL=open");
    }

    lastCursor_ = cursor_;
}

} // namespace ui
