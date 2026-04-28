#include "MainMenu.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "SubMenu.h"

namespace ui {

namespace {

struct Item { const char* name; const char* hint; };
const Item ITEMS[] = {
    { "Wi-Fi",    "packet mon, scan, deauth, portal" },
    { "Bluetooth","BLE scan, sniff, jam, spoof"      },
    { "2.4 GHz",  "NRF24 spectrum, protokill"        },
    { "Sub-GHz",  "CC1101 replay, jam, profiles"     },
    { "IR",       "record, replay, profiles"         },
    { "Tools",    "ducky, storage, diagnostics"      },
    { "Settings", "USB mode, brightness, theme"      },
    { "About",    "version, build, MAC"              },
};
constexpr int NUM_ITEMS = sizeof(ITEMS) / sizeof(ITEMS[0]);

constexpr int ITEM_H = 32;
constexpr int LIST_Y = 36;

} // namespace

void MainMenu::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);

    tft.fillRect(0, 0, 240, 30, p.headerBg);
    tft.setTextFont(4);
    tft.setTextColor(p.headerFg, p.headerBg);
    const char* title = "CTRL//VOID";
    int tw = tft.textWidth(title);
    tft.setCursor((240 - tw) / 2, 4);
    tft.print(title);

    lastCursor_ = -1;
    dirty();
}

bool MainMenu::onEvent(const input::Event& e) {
    using input::EventType;
    using input::Key;

    if (e.type == EventType::KeyDown || e.type == EventType::KeyRepeat) {
        switch (e.key) {
            case Key::Up:
                cursor_ = (cursor_ - 1 + NUM_ITEMS) % NUM_ITEMS;
                dirty();
                return true;
            case Key::Down:
                cursor_ = (cursor_ + 1) % NUM_ITEMS;
                dirty();
                return true;
            case Key::Select:
            case Key::Right:
                if (e.type == EventType::KeyDown) {
                    push(new SubMenu(ITEMS[cursor_].name));
                }
                return true;
            default: break;
        }
    }
    return false;
}

void MainMenu::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.setTextFont(2);
    const bool fullPaint = (lastCursor_ < 0);

    for (int i = 0; i < NUM_ITEMS; ++i) {
        if (!fullPaint && i != cursor_ && i != lastCursor_) continue;

        int y = LIST_Y + i * ITEM_H;
        bool sel = (i == cursor_);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.text;
        tft.fillRect(0, y, 240, ITEM_H - 2, bg);
        tft.setTextColor(fg, bg);
        tft.setCursor(16, y + 2);
        tft.print(ITEMS[i].name);
        tft.setTextFont(1);
        tft.setTextColor(sel ? p.accent : p.textDim, bg);
        tft.setCursor(16, y + 20);
        tft.print(ITEMS[i].hint);
        tft.setTextFont(2);
    }
    lastCursor_ = cursor_;
}

} // namespace ui
