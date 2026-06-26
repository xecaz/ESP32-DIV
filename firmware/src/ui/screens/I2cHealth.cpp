#include "I2cHealth.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../input/InputTask.h"

namespace ui {

void I2cHealthScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "I2C Health");
    input::resetPcfStats();
    auto s = input::pcfStats();
    prevOk_   = s.okCount;
    prevFail_ = s.failCount;
    lastUpdateMs_ = millis();
    okPerSec_ = failPerSec_ = 0;
    dirty();
}

bool I2cHealthScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    if (e.key == Key::Left) { pop(); return true; }
    if (e.key == Key::Select) {
        input::resetPcfStats();
        auto s = input::pcfStats();
        prevOk_ = s.okCount;
        prevFail_ = s.failCount;
        dirty();
        return true;
    }
    return false;
}

void I2cHealthScreen::onTick(uint32_t nowMs) {
    if (nowMs - lastUpdateMs_ < 500) return;
    auto s = input::pcfStats();
    uint32_t elapsed = nowMs - lastUpdateMs_;
    if (elapsed > 0) {
        okPerSec_   = (s.okCount   - prevOk_)   * 1000UL / elapsed;
        failPerSec_ = (s.failCount - prevFail_) * 1000UL / elapsed;
    }
    prevOk_   = s.okCount;
    prevFail_ = s.failCount;
    lastUpdateMs_ = nowMs;
    dirty();
}

void I2cHealthScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    auto s = input::pcfStats();

    tft.fillRect(0, 34, 240, 286, p.bg);

    // Big OK / FAIL rate pair — the headline number for the A/B test.
    tft.setTextFont(4);
    tft.setTextColor(p.ok, p.bg);
    tft.setCursor(8, 50);
    tft.printf("OK  %4lu/s", (unsigned long)okPerSec_);
    tft.setTextColor(p.warn, p.bg);
    tft.setCursor(8, 90);
    tft.printf("FAIL %4lu/s", (unsigned long)failPerSec_);

    // Hit-ratio bar — visual gauge of bus health.
    uint32_t total = okPerSec_ + failPerSec_;
    int      okPct = total ? (int)((okPerSec_ * 100UL) / total) : 0;
    tft.setTextFont(2);
    tft.setTextColor(p.text, p.bg);
    tft.setCursor(8, 140);
    tft.printf("hit ratio: %d%%", okPct);

    constexpr int BAR_X = 8, BAR_Y = 162, BAR_W = 224, BAR_H = 14;
    tft.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, p.text);
    int fill = (okPct * (BAR_W - 2)) / 100;
    uint16_t barColor = okPct >= 95 ? p.ok
                      : okPct >= 70 ? TFT_YELLOW : p.warn;
    tft.fillRect(BAR_X + 1, BAR_Y + 1, fill, BAR_H - 2, barColor);
    tft.fillRect(BAR_X + 1 + fill, BAR_Y + 1,
                 BAR_W - 2 - fill, BAR_H - 2, p.bg);

    // Cumulative counters. Of particular interest: INT vs timeout.
    // INT >> timeout means the bodge wire is working and we're reading
    // on demand. INT == 0 means INT isn't reaching the ESP32 — wire
    // loose / bad pad / wrong pin.
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 200);
    tft.printf("ok %lu  fail %lu",
               (unsigned long)s.okCount, (unsigned long)s.failCount);
    tft.setCursor(8, 218);
    tft.setTextColor(s.intCount ? p.ok : p.warn, p.bg);
    tft.printf("INT fires:   %lu", (unsigned long)s.intCount);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 236);
    tft.printf("tmo wakes:   %lu", (unsigned long)s.timeoutCount);
    tft.setCursor(8, 254);
    tft.printf("recoveries:  %lu", (unsigned long)s.recoverCount);

    tft.setCursor(8, 274);
    tft.setTextColor(s.lastOkAgeMs > 300 ? p.warn : p.text, p.bg);
    tft.printf("age %lums  raw 0x%02X",
               (unsigned long)s.lastOkAgeMs, s.latestRaw);

    theme::drawFooter(tft, "SEL=reset  LEFT=back");
}

} // namespace ui
