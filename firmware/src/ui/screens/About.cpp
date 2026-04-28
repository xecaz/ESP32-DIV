#include "About.h"

#include <TFT_eSPI.h>
#include <WiFi.h>

#include "../Theme.h"
#include "../UiTask.h"

namespace ui {

namespace {
constexpr const char* VERSION = "MK1 v0.9.0-dev";

const char* pageTitle(AboutScreen::Page p) {
    switch (p) {
        case AboutScreen::Page::Version: return "Version";
        case AboutScreen::Page::Mac:     return "MAC";
        case AboutScreen::Page::Credits: return "About";
        case AboutScreen::Page::License: return "License";
    }
    return "About";
}
}

void AboutScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, pageTitle(page_));
    dirty();
}

bool AboutScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    if (e.key == Key::Left) { pop(); return true; }
    return false;
}

void AboutScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 34, 240, 286, p.bg);
    tft.setTextFont(2);

    switch (page_) {
        case Page::Version: {
            tft.setTextColor(p.accent, p.bg);
            tft.setTextFont(4);
            tft.setCursor(28, 60);
            tft.print("CTRL//VOID");
            tft.setTextFont(2);
            tft.setTextColor(p.text, p.bg);
            tft.setCursor(80, 100);
            tft.print(VERSION);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 150);
            tft.printf("build date: %s", __DATE__);
            tft.setCursor(8, 168);
            tft.printf("build time: %s", __TIME__);
            tft.setCursor(8, 186);
            tft.print("chip:       ESP32-S3");
            tft.setCursor(8, 204);
            tft.print("flash:      16 MB");
            break;
        }
        case Page::Mac: {
            uint8_t mac[6]{};
            WiFi.macAddress(mac);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 70);
            tft.print("Device MAC (Wi-Fi STA)");
            tft.setTextColor(p.text, p.bg);
            tft.setTextFont(4);
            tft.setCursor(12, 100);
            tft.printf("%02X:%02X:%02X", mac[0], mac[1], mac[2]);
            tft.setCursor(12, 140);
            tft.printf("%02X:%02X:%02X", mac[3], mac[4], mac[5]);
            tft.setTextFont(2);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 210);
            tft.printf("OUI:  %02X:%02X:%02X", mac[0], mac[1], mac[2]);
            tft.setCursor(8, 228);
            tft.printf("NIC:  %02X:%02X:%02X", mac[3], mac[4], mac[5]);
            break;
        }
        case Page::Credits: {
            tft.setTextColor(p.ok, p.bg);
            tft.setCursor(8, 60);
            tft.print("CTRL//VOID firmware");
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 80);
            tft.print("Coded by Claude");
            tft.setCursor(8, 98);
            tft.print("with some heavy backseat");
            tft.setCursor(8, 116);
            tft.print("steering & bitching");
            tft.setCursor(8, 134);
            tft.print("by Xecaz");

            tft.setTextColor(p.accent, p.bg);
            tft.setCursor(8, 176);
            tft.print("Hardware");
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 196);
            tft.print("ESP32-DIV by CiferTech");
            tft.setTextFont(1);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 214);
            tft.print("github.com/cifertech");
            tft.setTextFont(2);

            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 248);
            tft.print("Licenses: both MIT");
            tft.setCursor(8, 266);
            tft.print("see About > License");
            break;
        }
        case Page::License: {
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 48);
            tft.print("CTRL//VOID FW: MIT");
            tft.setTextFont(1);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 66);
            tft.print("(c) 2026 Xecaz and Claude");
            tft.setTextFont(2);

            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 110);
            tft.print("Hardware: MIT");
            tft.setTextFont(1);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 128);
            tft.print("(c) 2023 CiferTech");
            tft.setCursor(8, 142);
            tft.print("github.com/cifertech/ESP32-DIV");

            tft.setTextFont(2);
            tft.setTextColor(p.textDim, p.bg);
            tft.setCursor(8, 190);
            tft.print("Full text: /LICENSE on SD");
            tft.setCursor(8, 208);
            tft.print("or in the repo root.");
            break;
        }
    }

    theme::drawFooter(tft, "LEFT = back");
}

} // namespace ui
