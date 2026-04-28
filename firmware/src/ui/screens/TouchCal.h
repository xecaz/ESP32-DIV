#pragma once

#include <stdint.h>
#include "../Screen.h"

namespace ui {

// Two-tap calibration: user taps top-left pixel, then bottom-right pixel.
// We record the raw XPT2046 readings at each, compute min/max per axis,
// and persist to NVS. Subsequent screens that use the pixel-mapped touch
// events get accurate coordinates.
class TouchCalScreen : public Screen {
public:
    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    // 5-point calibration: TL, TR, BR, BL, Center. Each tap records the
    // raw XPT2046 reading at the known pixel target. Using 4 corners
    // instead of 2 averages out user tap imprecision; the center tap
    // sanity-checks linearity (if the center is way off what our linear
    // mapping predicts, the panel has severe nonlinearity and we warn).
    enum class Stage : uint8_t {
        TapTL, TapTR, TapBR, TapBL, TapC, Done
    };

    Stage   stage_ = Stage::TapTL;
    struct P { int16_t x, y; };
    P       tl_{}, tr_{}, br_{}, bl_{}, c_{};
    uint32_t lastPromptMs_ = 0;
    int16_t  centerErrorPx_ = 0;   // post-Done: how far center was off
};

} // namespace ui
