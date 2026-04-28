#include "IrScreens.h"

#include <TFT_eSPI.h>
#include <IRremoteESP8266.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "Keyboard.h"
#include "../../hw/Board.h"
#include "../../radio/IrDriver.h"

namespace ui {

// ── Record ──────────────────────────────────────────────────────────────

void IrRecordScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "IR Record");
    radio::ir::clearCapture();
    radio::ir::startRx();
    dirty();
}

void IrRecordScreen::onExit(TFT_eSPI&) {
    radio::ir::stopRx();
}

bool IrRecordScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    switch (e.key) {
        case Key::Left: pop(); return true;
        case Key::Up:
            radio::ir::clearCapture();
            dirty();
            return true;
        case Key::Select: case Key::Right: {
            radio::ir::IrCapture c;
            if (!radio::ir::getCapture(c)) { dirty(); return true; }
            if (!board::sdMounted()) {
                // No point prompting for a name — save will fail. Surface
                // it so the user knows to insert a card.
                lastSave_ = "NO SD CARD";
                saveMs_   = millis();
                dirty();
                return true;
            }
            IrRecordScreen* self = this;
            push(new Keyboard("Name this IR capture:", "", [self](const String* t) {
                radio::ir::IrCapture cc;
                if (!radio::ir::getCapture(cc)) return;
                String label = (t && t->length()) ? *t : String("unnamed");
                String name  = radio::ir::saveCapture(cc, label.c_str());
                self->lastSave_ = name.length() ? name : String("SAVE FAILED");
                self->saveMs_   = millis();
                Serial.printf("[ir] save -> %s (label=%s)\n",
                              self->lastSave_.c_str(), label.c_str());
            }));
            return true;
        }
        default: return false;
    }
}

void IrRecordScreen::onTick(uint32_t nowMs) {
    bool now = radio::ir::haveCapture();
    if (now != lastHad_) { lastHad_ = now; dirty(); }
    // Keep the save-status banner visible/fading for the first 5 s.
    if (lastSave_.length() && nowMs - saveMs_ < 5000) {
        static uint32_t lastBannerMs = 0;
        if (nowMs - lastBannerMs > 300) { lastBannerMs = nowMs; dirty(); }
    }
}

void IrRecordScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 220, p.bg);
    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 50);
    tft.print("point remote at TSOP");
    tft.setCursor(8, 68);
    tft.print("on the shield.");

    // Last-save line only — the generic warning strip (drawn at the
    // bottom) covers the missing-SD case.
    if (lastSave_.length() && millis() - saveMs_ < 5000) {
        tft.setTextFont(1);
        tft.setTextColor(lastSave_.startsWith("ir_") ? p.ok : p.warn,
                         p.bg);
        tft.setCursor(8, 86);
        tft.printf("-> %s", lastSave_.c_str());
        tft.setTextFont(2);
    }

    radio::ir::IrCapture c;
    if (radio::ir::getCapture(c)) {
        tft.setTextColor(p.ok, p.bg);
        tft.setCursor(8, 110); tft.print("captured!");
        tft.setTextColor(TFT_YELLOW, p.bg);
        tft.setCursor(8, 130); tft.printf("value:  0x%08lX", (unsigned long)c.decodedValue);
        tft.setCursor(8, 148); tft.printf("bits:   %u", c.bits);
        tft.setCursor(8, 166); tft.printf("proto:  %s",
            c.protocol == UNKNOWN ? "raw" :
            c.protocol == NEC     ? "NEC" :
            c.protocol == SONY    ? "Sony" :
            c.protocol == RC5     ? "RC5" :
            c.protocol == RC6     ? "RC6" :
            c.protocol == SAMSUNG ? "Samsung" : "other");
        tft.setCursor(8, 184); tft.printf("raw len: %u", c.rawLen);
    } else {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 120); tft.print("listening...");
    }

    theme::drawSdWarningIfMissing(tft);
    theme::drawFooter(tft, "SEL=save  UP=clear  LEFT=back");
}

// (Old transient-capture IrReplayScreen removed — Replay is the saved-
// profile browser now. The IrProfilesScreen below IS that "Replay" screen.)

#if 0
void IrReplayScreen::onRender(TFT_eSPI& tft) {
    tft.fillRect(0, 40, 240, 220, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    radio::ir::IrCapture c;
    if (radio::ir::getCapture(c)) {
        tft.setCursor(8, 50);
        tft.printf("ready: 0x%08lX", (unsigned long)c.decodedValue);
        tft.setCursor(8, 68);
        tft.printf("%u bits  proto=%u", c.bits, c.protocol);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(8, 130);
        tft.printf("sent: %d", txCount_);
        if (lastTxMs_ && (millis() - lastTxMs_) < 300) {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setCursor(8, 150); tft.print("TX!");
        }
    } else {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(8, 110);
        tft.print("no capture to replay");
        tft.setCursor(8, 128);
        tft.print("run IR Record first");
    }

    tft.fillRect(0, 302, 240, 18, TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(8, 306);
    tft.print("SEL=send  LEFT=back");
    tft.setTextFont(2);
}
#endif

// ── Replay = Profiles browser ─────────────────────────────────────────

void IrProfilesScreen::reload() {
    count_ = radio::ir::listSaved(files_, MAX_FILES);
    if (cursor_ >= count_) cursor_ = count_ ? count_ - 1 : 0;
}

void IrProfilesScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "IR Replay");
    reload();
    dirty();
}

bool IrProfilesScreen::onEvent(const input::Event& e) {
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
            radio::ir::IrCapture c;
            if (radio::ir::loadCapture(files_[cursor_], c)) {
                radio::ir::txCapture(c);
                ++txCount_;
                lastTxMs_ = millis();
                dirty();
            }
            return true;
        }
        default: return false;
    }
}

void IrProfilesScreen::onTick(uint32_t nowMs) {
    // Blink the "TX!" flash for 250 ms after a send.
    static uint32_t blinkLast = 0;
    if (lastTxMs_ && nowMs - lastTxMs_ < 250 && nowMs != blinkLast) {
        blinkLast = nowMs;
        dirty();
    }
}

void IrProfilesScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 40, 240, 260, p.bg);

    if (count_ == 0) {
        tft.setTextFont(2);
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 80);
        tft.print("no profiles saved yet");
        tft.setCursor(8, 100);
        tft.print("capture + save via IR Record");
    } else {
        constexpr int ROW_H = 22;
        for (int i = 0; i < count_ && i < 10; ++i) {
            int y = 40 + i * ROW_H;
            bool sel = (i == cursor_);
            uint16_t bg = sel ? p.selBg : p.bg;
            uint16_t fg = sel ? p.selFg : p.textDim;
            tft.fillRect(0, y, 240, ROW_H - 2, bg);
            tft.setTextFont(2);
            tft.setTextColor(fg, bg);
            tft.setCursor(8, y + 2);
            tft.print(files_[i]);
        }
        if (lastTxMs_ && millis() - lastTxMs_ < 250) {
            tft.setTextColor(p.ok, p.bg);
            tft.setCursor(8, 260);
            tft.printf("TX! (%d)", txCount_);
        }
    }

    theme::drawSdWarningIfMissing(tft);
    theme::drawFooter(tft, "SEL=send  LEFT=back");
}

} // namespace ui
