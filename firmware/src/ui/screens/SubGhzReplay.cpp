#include "SubGhzReplay.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "Keyboard.h"
#include "../../hw/Board.h"
#include "../../radio/Cc1101Driver.h"

namespace ui {

// ── Replay ──────────────────────────────────────────────────────────────

void SubGhzReplayScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "SubGhz Replay");
    radio::cc1101::startRx(freqHz_);
    dirty();
}

void SubGhzReplayScreen::onExit(TFT_eSPI&) {
    radio::cc1101::stopRx();
}

bool SubGhzReplayScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    // Tap on the frequency line (top area of the body, below header) opens
    // the numeric keyboard to type a frequency directly. Guarded by
    // kbActive_ so rapid taps don't queue multiple keyboard pushes — that
    // pattern was crashing the device under fast fingertapping.
    if (e.type == EventType::TouchDown && e.y >= 40 && e.y <= 90 && !kbActive_) {
        kbActive_ = true;
        char buf[16];
        snprintf(buf, sizeof(buf), "%.3f", freqHz_ / 1000000.0);
        SubGhzReplayScreen* self = this;
        radio::cc1101::stopRx();
        push(new Keyboard("Freq MHz:", String(buf), [self](const String* t) {
            if (t) {
                float mhz = t->toFloat();
                if (mhz > 0 && mhz < 2000) {
                    self->freqHz_ = (uint32_t)(mhz * 1000000.0f + 0.5f);
                }
            }
            radio::cc1101::startRx(self->freqHz_);
            self->kbActive_ = false;
            self->dirty();
        }, /*mask=*/false, Keyboard::StartLayer::Numeric));
        return true;
    }

    if (e.type != EventType::KeyDown) return false;
    switch (e.key) {
        case Key::Left: pop(); return true;
        case Key::Up:   freqHz_ += 100000; dirty(); return true;
        case Key::Down: freqHz_ -= 100000; dirty(); return true;
        case Key::Right: {
            // Replay (transient — doesn't save).
            radio::cc1101::Capture c;
            if (radio::cc1101::rxLatestCapture(c)) {
                radio::cc1101::stopRx();
                radio::cc1101::txCapture(c);
                radio::cc1101::startRx(freqHz_);
            }
            dirty();
            return true;
        }
        case Key::Select: {
            // Save to SD with a user-typed label (like IR Record).
            radio::cc1101::Capture c;
            if (!radio::cc1101::rxLatestCapture(c)) { dirty(); return true; }
            if (!board::sdMounted()) {
                lastSave_ = "NO SD CARD";
                saveMs_ = millis();
                dirty();
                return true;
            }
            SubGhzReplayScreen* self = this;
            push(new Keyboard("Name this sub-GHz capture:", "",
                              [self](const String* t) {
                radio::cc1101::Capture cc;
                if (!radio::cc1101::rxLatestCapture(cc)) return;
                String label = (t && t->length()) ? *t : String("unnamed");
                String name  = radio::cc1101::saveCaptureToSd(cc, label.c_str());
                self->lastSave_ = name.length() ? name : String("SAVE FAILED");
                self->saveMs_   = millis();
            }));
            return true;
        }
        default: return false;
    }
}

void SubGhzReplayScreen::onTick(uint32_t) {
    bool now = radio::cc1101::rxHaveCapture();
    if (now != lastHadCapture_) { lastHadCapture_ = now; dirty(); }
}

void SubGhzReplayScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);

    // Draw freq line as a tappable "button" — boxed + subtle highlight so
    // it's obvious it can be touched to enter a new frequency directly.
    tft.fillRect(2, 44, 236, 28, p.fieldBg);
    tft.drawRect(2, 44, 236, 28, p.accent);
    tft.setTextColor(p.accent, p.fieldBg);
    tft.setCursor(8, 50);  tft.printf("freq: %lu.%03lu MHz",
        (unsigned long)(freqHz_/1000000),
        (unsigned long)((freqHz_/1000) % 1000));
    tft.setTextFont(1);
    tft.setTextColor(p.textDim, p.fieldBg);
    tft.setCursor(180, 52);
    tft.print("tap to edit");
    tft.setTextFont(2);

    tft.setCursor(8, 74);
    tft.setTextColor(p.textDim, p.bg);
    tft.print(radio::cc1101::rxRunning() ? "listening..." : "idle");

    radio::cc1101::Capture c;
    if (radio::cc1101::rxLatestCapture(c)) {
        tft.setTextColor(TFT_YELLOW, p.bg);
        tft.setCursor(8, 110); tft.printf("value:    0x%08lX", (unsigned long)c.value);
        tft.setCursor(8, 128); tft.printf("bits:     %u", c.bitLength);
        tft.setCursor(8, 146); tft.printf("protocol: %u", c.protocol);
    } else {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 110); tft.print("no capture yet");
        tft.setCursor(8, 128); tft.print("press a remote");
    }

    // Last-save line (the generic SD warning strip handles the missing case).
    if (lastSave_.length() && millis() - saveMs_ < 5000) {
        tft.setTextFont(1);
        tft.setTextColor(lastSave_.startsWith("SAVE") || lastSave_.startsWith("NO")
                             ? p.warn : p.ok, p.bg);
        tft.setCursor(8, 170);
        tft.printf("-> %s", lastSave_.c_str());
        tft.setTextFont(2);
    }

    theme::drawSdWarningIfMissing(tft);
    theme::drawFooter(tft, "U/D=step RIGHT=tx SEL=save tap freq");
}

// ── Jammer ──────────────────────────────────────────────────────────────

void SubGhzJammerScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "SubGhz Jam");
    armed_ = false;
    dirty();
}

void SubGhzJammerScreen::onExit(TFT_eSPI&) {
    radio::cc1101::stopJammer();
}

bool SubGhzJammerScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    switch (e.key) {
        case Key::Left: pop(); return true;
        case Key::Up:   if (!armed_) freqHz_ += 100000; dirty(); return true;
        case Key::Down: if (!armed_) freqHz_ -= 100000; dirty(); return true;
        case Key::Select: case Key::Right:
            armed_ = !armed_;
            if (armed_) radio::cc1101::startJammer(freqHz_);
            else        radio::cc1101::stopJammer();
            dirty();
            return true;
        default: return false;
    }
}

void SubGhzJammerScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);
    tft.setTextFont(4);
    tft.setTextColor(armed_ ? p.warn : p.textDim, p.bg);
    tft.setCursor(40, 60);
    tft.print(armed_ ? "ARMED" : "IDLE");

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 110);
    tft.printf("freq: %lu.%03lu MHz",
        (unsigned long)(freqHz_/1000000),
        (unsigned long)((freqHz_/1000) % 1000));

    tft.setTextColor(TFT_ORANGE, p.bg);
    tft.setCursor(8, 160);
    tft.print("check local regs!");

    theme::drawFooter(tft, "U/D=freq  SEL=arm  LEFT=back");
}

// ── Profiles browser ───────────────────────────────────────────────────

void SubGhzProfilesScreen::reload() {
    count_ = radio::cc1101::listSaved(files_, MAX_FILES);
    if (cursor_ >= count_) cursor_ = count_ ? count_ - 1 : 0;
}

void SubGhzProfilesScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Sub-GHz TX");
    reload();
    dirty();
}

bool SubGhzProfilesScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;
    switch (e.key) {
        case Key::Left:
            if (e.type == EventType::KeyDown) pop();
            return true;
        case Key::Up:
            if (count_) { cursor_ = (cursor_ - 1 + count_) % count_; dirty(); }
            return true;
        case Key::Down:
            if (count_) { cursor_ = (cursor_ + 1) % count_; dirty(); }
            return true;
        case Key::Select: case Key::Right: {
            if (e.type != EventType::KeyDown || !count_) return true;
            radio::cc1101::Capture c;
            if (radio::cc1101::loadCapture(files_[cursor_], c)) {
                radio::cc1101::txCapture(c);
                ++txCount_;
                lastTxMs_ = millis();
                dirty();
            }
            return true;
        }
        default: return false;
    }
}

void SubGhzProfilesScreen::onTick(uint32_t nowMs) {
    if (lastTxMs_ && nowMs - lastTxMs_ < 300) {
        static uint32_t prev = 0;
        if (nowMs - prev > 100) { prev = nowMs; dirty(); }
    }
}

void SubGhzProfilesScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 260, p.bg);
    tft.setTextFont(2);
    if (count_ == 0) {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 80);
        tft.print("no profiles saved yet");
        tft.setCursor(8, 100);
        tft.print("capture + save via Record");
    } else {
        constexpr int ROW_H = 22;
        for (int i = 0; i < count_ && i < 10; ++i) {
            int y = 40 + i * ROW_H;
            bool sel = (i == cursor_);
            uint16_t bg = sel ? p.selBg : p.bg;
            uint16_t fg = sel ? p.selFg : p.textDim;
            tft.fillRect(0, y, 240, ROW_H - 2, bg);
            tft.setTextColor(fg, bg);
            tft.setCursor(8, y + 2);
            tft.print(files_[i]);
        }
        if (lastTxMs_ && millis() - lastTxMs_ < 300) {
            tft.setTextColor(p.ok, p.bg);
            tft.setCursor(8, 260);
            tft.printf("TX! (%d)", txCount_);
        }
    }
    theme::drawSdWarningIfMissing(tft);
    theme::drawFooter(tft, "SEL=send  LEFT=back");
}

} // namespace ui
