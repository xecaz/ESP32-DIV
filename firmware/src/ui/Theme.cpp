#include "Theme.h"

#include <TFT_eSPI.h>

#include "../hw/Board.h"
#include "../storage/Settings.h"

namespace ui { namespace theme {

namespace {
// Pre-baked palettes. Keep these readable — first-match-wins on screen
// role so tweaking one swatch gives a consistent feel.
const Palette DARK {
    /*bg*/       TFT_BLACK,
    /*headerBg*/ TFT_NAVY,
    /*headerFg*/ TFT_WHITE,
    /*text*/     TFT_WHITE,
    /*textDim*/  TFT_LIGHTGREY,
    /*accent*/   TFT_CYAN,
    /*selBg*/    TFT_DARKGREEN,
    /*selFg*/    TFT_WHITE,
    /*fieldBg*/  0x20A2,
    /*warn*/     TFT_RED,
    /*ok*/       TFT_GREEN,
};
const Palette LIGHT {
    0xE71C,             // very light grey body
    0x4208,             // dark grey header
    TFT_WHITE,          // header text
    TFT_BLACK,          // body text
    0x4208,             // dim
    TFT_NAVY,           // accent
    TFT_NAVY,           // selected bg
    TFT_WHITE,          // selected fg
    0xFFFF,             // field (pure white)
    TFT_RED,
    0x0320,             // darker green
};
const Palette HACKER {
    TFT_BLACK,
    TFT_BLACK,
    TFT_GREEN,
    TFT_GREEN,
    0x03E0,             // mid green for dim
    TFT_GREEN,
    0x0320,
    TFT_WHITE,
    0x0060,             // faint green field
    TFT_YELLOW,
    TFT_GREEN,
};
const Palette AMBER {
    0x1001,             // very dark brown body
    0x2000,             // bronze header
    0xFD20,             // amber/orange text
    0xFD80,
    0xFCE0,
    0xFD80,
    0xFD20,
    TFT_BLACK,
    0x0841,             // faint amber field
    TFT_RED,
    0xFFE0,
};

Palette g_current = DARK;
}

const Palette& palette() { return g_current; }

void apply() {
    switch (storage::get().theme) {
        case 0: g_current = DARK;   break;
        case 1: g_current = LIGHT;  break;
        case 2: g_current = HACKER; break;
        case 3: g_current = AMBER;  break;
        default: g_current = DARK;  break;
    }
}

void drawHeader(TFT_eSPI& tft, const char* title) {
    const auto& p = g_current;
    tft.fillRect(0, 0, 240, 30, p.headerBg);
    tft.setTextFont(4);
    tft.setTextColor(p.headerFg, p.headerBg);
    tft.setCursor(12, 4);
    tft.print(title);
    tft.setTextFont(2);
}

void clearBody(TFT_eSPI& tft) {
    tft.fillRect(0, 30, 240, 272, g_current.bg);
}

void drawFooter(TFT_eSPI& tft, const char* hint) {
    const auto& p = g_current;
    tft.fillRect(0, 302, 240, 18, p.bg);
    tft.setTextFont(1);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 306);
    tft.print(hint);
    tft.setTextFont(2);
}

void drawSdWarningIfMissing(TFT_eSPI& tft) {
    if (board::sdMounted()) return;  // silent when OK, per user request
    const auto& p = g_current;
    // Strip spans the 18 px just above the footer hint.
    tft.fillRect(0, 282, 240, 18, p.warn);
    tft.setTextFont(1);
    tft.setTextColor(p.selFg, p.warn);
    tft.setCursor(8, 286);
    tft.print("!! no SD card — insert to save/load");
    tft.setTextFont(2);
}

}} // namespace ui::theme
