#include "TouchCal.h"

#include <TFT_eSPI.h>
#include <math.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../input/InputTask.h"
#include "../../storage/Settings.h"

namespace ui {

namespace {
// Cross is 21 px tall/wide centered on its (x,y). With y=306 the bottom
// arms clip off the 320-px panel — shift the bottom row up so the full
// cross is on screen.
constexpr int MARGIN_X = 14;
constexpr int MARGIN_TOP    = 14;
constexpr int MARGIN_BOTTOM = 26;
constexpr int TL_X = MARGIN_X;
constexpr int TL_Y = MARGIN_TOP;
constexpr int TR_X = 240 - MARGIN_X;
constexpr int TR_Y = MARGIN_TOP;
constexpr int BR_X = 240 - MARGIN_X;
constexpr int BR_Y = 320 - MARGIN_BOTTOM;
constexpr int BL_X = MARGIN_X;
constexpr int BL_Y = 320 - MARGIN_BOTTOM;
constexpr int CC_X = 120;
constexpr int CC_Y = 160;

void drawCross(TFT_eSPI& tft, int x, int y, uint16_t color) {
    tft.drawFastVLine(x, y - 10, 21, color);
    tft.drawFastHLine(x - 10, y, 21, color);
    tft.drawCircle(x, y, 4, color);
}
}

void TouchCalScreen::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);
    tft.setTextFont(4);
    tft.setTextColor(p.text, p.bg);
    tft.setCursor(40, 80);
    tft.print("Touch Cal");
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(24, 130);
    tft.print("tap each cross exactly");
    dirty();
}

bool TouchCalScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    // LEFT at any time cancels without saving.
    if (e.type == EventType::KeyDown && e.key == Key::Left) {
        pop();
        return true;
    }

    if (e.type != EventType::TouchDown) return false;

    // Snapshot the raw reading (not pixel-mapped; that's the whole point).
    auto rt = input::lastRawTouch();
    P here{ rt.x, rt.y };

    switch (stage_) {
        case Stage::TapTL: tl_ = here; stage_ = Stage::TapTR; dirty(); break;
        case Stage::TapTR: tr_ = here; stage_ = Stage::TapBR; dirty(); break;
        case Stage::TapBR: br_ = here; stage_ = Stage::TapBL; dirty(); break;
        case Stage::TapBL: bl_ = here; stage_ = Stage::TapC;  dirty(); break;
        case Stage::TapC: {
            c_ = here;

            // Detect axis swap: how much does raw X vary moving across the
            // screen horizontally (TL→TR) vs vertically (TL→BL)? The
            // axis that changes most between TL and TR is the one that
            // corresponds to screen X.
            int dxH = abs(tr_.x - tl_.x) + abs(br_.x - bl_.x);
            int dyH = abs(tr_.y - tl_.y) + abs(br_.y - bl_.y);
            bool swap = (dyH > dxH);

            // Extract the raw components that align with screen X and Y.
            auto rx = [&](const P& p) { return swap ? p.y : p.x; };
            auto ry = [&](const P& p) { return swap ? p.x : p.y; };
            int tlRx = rx(tl_), trRx = rx(tr_), brRx = rx(br_), blRx = rx(bl_);
            int tlRy = ry(tl_), trRy = ry(tr_), brRy = ry(br_), blRy = ry(bl_);

            // Averaged bounds per axis.
            int leftX  = (tlRx + blRx) / 2;   // raw at screen-left
            int rightX = (trRx + brRx) / 2;   // raw at screen-right
            int topY   = (tlRy + trRy) / 2;   // raw at screen-top
            int botY   = (blRy + brRy) / 2;   // raw at screen-bottom

            int xMin = (leftX < rightX) ? leftX : rightX;
            int xMax = (leftX > rightX) ? leftX : rightX;
            int yMin = (topY  < botY)  ? topY  : botY;
            int yMax = (topY  > botY)  ? topY  : botY;

            // Direction flags: if raw at screen-left > raw at screen-right,
            // the axis is mirrored relative to the screen so we invert.
            bool invX = (leftX > rightX);
            bool invY = (topY  > botY);

            // Linearity check using the detected orientation.
            auto mapWith = [&](int raw, int rMin, int rMax, int pxMax, bool inv) {
                int v = map(raw, rMin, rMax, 0, pxMax);
                if (v < 0) v = 0; if (v > pxMax) v = pxMax;
                return inv ? (pxMax - v) : v;
            };
            int centerRx = rx(c_), centerRy = ry(c_);
            int predX = mapWith(centerRx, xMin, xMax, 239, invX);
            int predY = mapWith(centerRy, yMin, yMax, 319, invY);
            int dxE = predX - CC_X, dyE = predY - CC_Y;
            centerErrorPx_ = (int16_t)sqrtf((float)(dxE * dxE + dyE * dyE));

            if (xMax - xMin > 500 && yMax - yMin > 500) {
                auto& s = storage::mut();
                // Always stored in "native" axis order: touchXMin/Max is
                // the raw axis that goes with screen X, whether or not
                // that's raw X or raw Y on the panel. The orientation
                // flags tell the input task how to find it.
                if (swap) {
                    // The X-axis-in-settings refers to raw Y (swap means
                    // raw Y varies with screen X). So store our chosen
                    // leftX/rightX (which came from raw Y) into tYMin/Max
                    // for clarity when swap=true.
                    s.touchYMin = (uint16_t)xMin; s.touchYMax = (uint16_t)xMax;
                    s.touchXMin = (uint16_t)yMin; s.touchXMax = (uint16_t)yMax;
                } else {
                    s.touchXMin = (uint16_t)xMin; s.touchXMax = (uint16_t)xMax;
                    s.touchYMin = (uint16_t)yMin; s.touchYMax = (uint16_t)yMax;
                }
                s.touchSwapXY  = swap;
                s.touchInvertX = invX;
                s.touchInvertY = invY;
                storage::save();
            }
            stage_ = Stage::Done;
            dirty();
            break;
        }
        case Stage::Done:
            pop();
            break;
    }
    return true;
}

void TouchCalScreen::onTick(uint32_t nowMs) {
    if (nowMs - lastPromptMs_ > 500) { lastPromptMs_ = nowMs; dirty(); }
}

void TouchCalScreen::onRender(TFT_eSPI& tft) {
    const auto& pal = theme::palette();
    tft.fillScreen(pal.bg);

    auto cross = [&](Stage s, int x, int y) {
        uint16_t color = (stage_ == s)   ? TFT_YELLOW
                       : ((int)stage_ > (int)s) ? pal.ok
                                                : pal.textDim;
        drawCross(tft, x, y, color);
    };
    cross(Stage::TapTL, TL_X, TL_Y);
    cross(Stage::TapTR, TR_X, TR_Y);
    cross(Stage::TapBR, BR_X, BR_Y);
    cross(Stage::TapBL, BL_X, BL_Y);
    cross(Stage::TapC,  CC_X, CC_Y);

    tft.setTextFont(2);
    tft.setTextColor(pal.textDim, pal.bg);
    tft.setCursor(8, 130);

    switch (stage_) {
        case Stage::TapTL: tft.print("tap TOP-LEFT cross");     break;
        case Stage::TapTR: tft.print("tap TOP-RIGHT cross");    break;
        case Stage::TapBR: tft.print("tap BOTTOM-RIGHT cross"); break;
        case Stage::TapBL: tft.print("tap BOTTOM-LEFT cross");  break;
        case Stage::TapC:  tft.print("tap CENTER cross");       break;
        case Stage::Done: {
            tft.setTextColor(pal.ok, pal.bg);
            tft.print("saved!");
            const auto& s = storage::get();
            tft.setTextColor(pal.textDim, pal.bg);
            tft.setCursor(8, 150);
            tft.printf("x: %u..%u", s.touchXMin, s.touchXMax);
            tft.setCursor(8, 168);
            tft.printf("y: %u..%u", s.touchYMin, s.touchYMax);
            // Linearity: 0-12 px is excellent, 13-25 px is usable,
            // 26+ px means the panel is non-linear and a simple linear
            // map will miss adjacent keys near the edges.
            tft.setCursor(8, 186);
            uint16_t c = centerErrorPx_ <= 12 ? pal.ok
                       : centerErrorPx_ <= 25 ? TFT_YELLOW : pal.warn;
            tft.setTextColor(c, pal.bg);
            tft.printf("center err: %d px", centerErrorPx_);
            tft.setTextColor(pal.textDim, pal.bg);
            tft.setCursor(8, 220);
            tft.print("tap anywhere to exit");
            break;
        }
    }

    theme::drawFooter(tft, "LEFT = cancel");
}

} // namespace ui
