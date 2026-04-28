#include "ThemePick.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../storage/Settings.h"

namespace ui {

namespace {
struct Opt { const char* name; uint16_t bg; uint16_t fg; uint16_t accent; };
const Opt OPTIONS[] = {
    {"Dark",   TFT_BLACK,     TFT_LIGHTGREY, TFT_CYAN   },
    {"Light",  0xE71C,        TFT_BLACK,     TFT_NAVY   },
    {"Hacker", TFT_BLACK,     TFT_GREEN,     TFT_GREEN  },
    {"Amber",  0x1001,        0xFCE0,        0xFD80     },
};
constexpr int N = sizeof(OPTIONS) / sizeof(OPTIONS[0]);
}

void ThemePickScreen::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);
    tft.fillRect(0, 0, 240, 30, p.headerBg);
    tft.setTextFont(4);
    tft.setTextColor(p.headerFg, p.headerBg);
    tft.setCursor(12, 4);
    tft.print("Theme");

    selected_ = storage::get().theme;
    if (selected_ >= N) selected_ = 0;
    dirty();
}

bool ThemePickScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type == EventType::KeyDown && e.key == Key::Left) { pop(); return true; }

    if (e.type == EventType::KeyDown || e.type == EventType::KeyRepeat) {
        if (e.key == Key::Up)   { selected_ = (selected_ - 1 + N) % N; dirty(); return true; }
        if (e.key == Key::Down) { selected_ = (selected_ + 1) % N;     dirty(); return true; }
        if ((e.key == Key::Select || e.key == Key::Right) &&
            e.type == EventType::KeyDown) {
            storage::mut().theme = selected_;
            storage::save();
            theme::apply();
            repaintTop();
            return true;
        }
    }
    if (e.type == EventType::TouchDown) {
        int row = (e.y - 48) / 56;
        if (row >= 0 && row < N) {
            selected_ = row;
            storage::mut().theme = selected_;
            storage::save();
            theme::apply();
            repaintTop();
            return true;
        }
    }
    return false;
}

void ThemePickScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 34, 240, 286, p.bg);
    tft.setTextFont(2);

    for (int i = 0; i < N; ++i) {
        int y = 48 + i * 56;
        bool sel = (i == selected_);

        // Row background: big swatch in the theme's own bg so the
        // preview matches the theme's palette.
        tft.fillRect(8, y, 224, 48, OPTIONS[i].bg);
        uint16_t border = sel ? TFT_YELLOW : OPTIONS[i].accent;
        tft.drawRect(8, y, 224, 48, border);
        // Double-thick border on selected row so it's impossible to miss.
        if (sel) tft.drawRect(7, y - 1, 226, 50, TFT_YELLOW);

        // Radio dot (outer ring + filled core when selected).
        int rx = 28, ry = y + 24;
        tft.drawCircle(rx, ry, 9, OPTIONS[i].accent);
        tft.drawCircle(rx, ry, 8, OPTIONS[i].accent);
        if (sel) {
            tft.fillCircle(rx, ry, 5, TFT_YELLOW);
        } else {
            tft.fillCircle(rx, ry, 5, OPTIONS[i].bg);
        }

        tft.setTextColor(OPTIONS[i].fg, OPTIONS[i].bg);
        tft.setCursor(50, y + 8);
        tft.setTextFont(4);
        tft.print(OPTIONS[i].name);
        tft.setTextFont(2);
        tft.setTextColor(OPTIONS[i].accent, OPTIONS[i].bg);
        tft.setCursor(50, y + 32);
        tft.print("sample accent");
    }

    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 278);
    tft.print("applies instantly — back out to see");

    tft.fillRect(0, 302, 240, 18, p.bg);
    tft.setTextFont(1);
    tft.setCursor(8, 306);
    tft.print("SEL/tap = choose   LEFT = back");
    tft.setTextFont(2);
}

} // namespace ui
