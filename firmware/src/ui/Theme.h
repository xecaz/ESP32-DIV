#pragma once

#include <stdint.h>

class TFT_eSPI;

namespace ui { namespace theme {

// Named slots every screen can pull from. Widgets that want to stay
// theme-aware reference these instead of hardcoded TFT_* colors.
struct Palette {
    uint16_t bg;          // screen body background
    uint16_t headerBg;    // top title-bar background
    uint16_t headerFg;    // top title-bar text
    uint16_t text;        // body text
    uint16_t textDim;     // secondary/dim body text
    uint16_t accent;      // highlight / interactive border
    uint16_t selBg;       // selected-row background
    uint16_t selFg;       // selected-row text
    uint16_t fieldBg;     // tappable input field background
    uint16_t warn;        // warning/error text
    uint16_t ok;          // success text
};

// Read the current palette. Cached — cheap to call per-frame.
const Palette& palette();

// Force a refresh from storage::get().theme. Called after the user
// picks a new theme (ThemePickScreen).
void apply();

// ── Common chrome helpers ─────────────────────────────────────────────
// Every screen uses the same header/body/footer layout. Using helpers
// means "theme switch = single palette lookup" rather than touching
// every screen's hardcoded TFT_NAVY / TFT_BLACK.
void drawHeader(TFT_eSPI& tft, const char* title);   // 30 px title bar
void clearBody (TFT_eSPI& tft);                      // wipe y=34..301
void drawFooter(TFT_eSPI& tft, const char* hint);    // 18 px hint strip

// Paints a red "no SD card" strip just above the footer (y=282, h=18)
// when board::sdMounted() is false. When the card is in, it's a no-op —
// no "SD OK" chrome, per the user's request. Call it at the end of
// onRender() on any SD-dependent screen.
void drawSdWarningIfMissing(TFT_eSPI& tft);

}} // namespace ui::theme
