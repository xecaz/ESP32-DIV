#include "Brightness.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../hw/Board.h"
#include "../../storage/Settings.h"

namespace ui {

namespace {
constexpr uint8_t MIN_VAL  = 10;    // anything lower = functionally off
constexpr uint8_t STEP     = 10;
constexpr int     BAR_X    = 20;
constexpr int     BAR_Y    = 140;
constexpr int     BAR_W    = 200;
constexpr int     BAR_H    = 30;
}

void BrightnessScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Brightness");

    initial_ = storage::get().brightness;
    value_   = initial_;
    apply();
    dirty();
}

void BrightnessScreen::onExit(TFT_eSPI&) {
    // If user backs out without SELECTing, restore the original so a
    // preview session doesn't quietly change their saved value.
    board::setBacklight(storage::get().brightness);
}

void BrightnessScreen::apply() {
    if (value_ < MIN_VAL) value_ = MIN_VAL;
    board::setBacklight(value_);
}

void BrightnessScreen::saveAndExit() {
    auto& s = storage::mut();
    s.brightness = value_;
    storage::save();
    initial_ = value_;  // so onExit doesn't revert
    pop();
}

bool BrightnessScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    // Touch the bar to jump to that position.
    if (e.type == EventType::TouchDown &&
        e.y >= BAR_Y - 10 && e.y <= BAR_Y + BAR_H + 10) {
        int rel = e.x - BAR_X;
        if (rel < 0) rel = 0;
        if (rel > BAR_W) rel = BAR_W;
        int v = (int)MIN_VAL + (int)(255 - MIN_VAL) * rel / BAR_W;
        value_ = (uint8_t)v;
        apply();
        dirty();
        return true;
    }

    if (e.type == EventType::KeyDown || e.type == EventType::KeyRepeat) {
        switch (e.key) {
            case Key::Up: case Key::Right:
                if (value_ <= 255 - STEP) value_ += STEP; else value_ = 255;
                apply(); dirty(); return true;
            case Key::Down: case Key::Left:
                if (e.key == Key::Left && e.type == EventType::KeyDown) {
                    // LEFT on first tap = back out (no save).
                    board::setBacklight(initial_);
                    pop();
                    return true;
                }
                if (value_ >= MIN_VAL + STEP) value_ -= STEP;
                else value_ = MIN_VAL;
                apply(); dirty(); return true;
            case Key::Select:
                if (e.type == EventType::KeyDown) saveAndExit();
                return true;
            default: return false;
        }
    }
    return false;
}

void BrightnessScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 34, 240, 286, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 60);
    tft.printf("%u / 255", value_);

    // Bar: outline + filled portion reflecting value.
    tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, p.text);
    int fillW = (int)value_ * (BAR_W - 2) / 255;
    tft.fillRect(BAR_X + 1, BAR_Y + 1, fillW, BAR_H - 2,
                 value_ > 200 ? p.ok :
                 value_ > 80  ? TFT_YELLOW : p.textDim);
    tft.fillRect(BAR_X + 1 + fillW, BAR_Y + 1,
                 BAR_W - 2 - fillW, BAR_H - 2, p.bg);

    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 200);
    tft.print("tap bar or UP/DOWN");
    tft.setCursor(8, 218);
    tft.print("SEL = save   LEFT = cancel");
    tft.setTextFont(2);
}

} // namespace ui
