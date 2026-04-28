#include "LedSettings.h"

#include <TFT_eSPI.h>
#include <stdlib.h>   // strtoul

#include "../Theme.h"
#include "../UiTask.h"
#include "Keyboard.h"
#include "../../hw/Leds.h"
#include "../../storage/Settings.h"

namespace ui {

namespace {

constexpr int ROW_H = 26;
constexpr int LIST_Y = 40;
constexpr int VISIBLE = 10;

struct Row {
    const char* label;
    enum class Kind : uint8_t {
        BoolToggle, UInt, HexColor, Button
    } kind;
    // For Bool/UInt/HexColor: member pointer selector; for Button: 0.
    // We just switch by row index in activate(), so a tag is enough.
    uint8_t tag;
};

// Row tags keep activate() and the accessors in sync.
enum : uint8_t {
    TagEnable = 0,
    TagPin,
    TagBrightness,
    TagTest,
    TagRxWifi, TagTxWifi,
    TagRxBle,  TagTxBle,
    TagRxSg,   TagTxSg,
    TagRxNf,   TagTxNf,
    TagEND
};

const Row ROWS[] = {
    {"Enabled",        Row::Kind::BoolToggle, TagEnable},
    {"LED data pin",   Row::Kind::UInt,       TagPin},
    {"Brightness",     Row::Kind::UInt,       TagBrightness},
    {"Run test",       Row::Kind::Button,     TagTest},
    {"Wi-Fi RX color", Row::Kind::HexColor,   TagRxWifi},
    {"Wi-Fi TX color", Row::Kind::HexColor,   TagTxWifi},
    {"BLE RX color",   Row::Kind::HexColor,   TagRxBle},
    {"BLE TX color",   Row::Kind::HexColor,   TagTxBle},
    {"Sub-GHz RX",     Row::Kind::HexColor,   TagRxSg},
    {"Sub-GHz TX",     Row::Kind::HexColor,   TagTxSg},
    {"2.4GHz RX",      Row::Kind::HexColor,   TagRxNf},
    {"2.4GHz TX",      Row::Kind::HexColor,   TagTxNf},
};
constexpr int ROW_COUNT = sizeof(ROWS) / sizeof(ROWS[0]);

uint32_t& colorRef(uint8_t tag) {
    auto& s = storage::mut();
    switch (tag) {
        case TagRxWifi: return s.ledRxWifi;
        case TagTxWifi: return s.ledTxWifi;
        case TagRxBle:  return s.ledRxBle;
        case TagTxBle:  return s.ledTxBle;
        case TagRxSg:   return s.ledRxSubghz;
        case TagTxSg:   return s.ledTxSubghz;
        case TagRxNf:   return s.ledRxNrf24;
        case TagTxNf:   return s.ledTxNrf24;
    }
    static uint32_t zero = 0;
    return zero;
}

// Convert "#RRGGBB" / "RRGGBB" / "0xRRGGBB" to uint32. Invalid input → 0.
uint32_t parseHex(const String& in) {
    const char* p = in.c_str();
    while (*p == '#' || *p == ' ') ++p;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    return (uint32_t)strtoul(p, nullptr, 16) & 0xFFFFFF;
}

String fmtHex(uint32_t c) {
    char buf[10];
    snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)(c & 0xFFFFFF));
    return String(buf);
}

} // namespace

void LedSettingsScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "LEDs");
    dirty();
}

void LedSettingsScreen::activate(int row) {
    auto& s = storage::mut();
    uint8_t tag = ROWS[row].tag;

    switch (tag) {
        case TagEnable:
            s.ledEnabled = !s.ledEnabled;
            storage::save();
            leds::reload();
            if (!s.ledEnabled) leds::allOff();
            break;
        case TagPin: {
            LedSettingsScreen* self = this;
            push(new Keyboard("LED data pin:", String(s.ledPin),
                              [self](const String* t) {
                if (t) {
                    int v = t->toInt();
                    if (v >= 0 && v <= 48) {
                        storage::mut().ledPin = (uint8_t)v;
                        storage::save();
                        leds::reload();
                    }
                }
                self->dirty();
            }, /*mask=*/false, Keyboard::StartLayer::Numeric));
            break;
        }
        case TagBrightness: {
            LedSettingsScreen* self = this;
            push(new Keyboard("Brightness 0-255:", String(s.ledBrightness),
                              [self](const String* t) {
                if (t) {
                    int v = t->toInt();
                    if (v < 0) v = 0; if (v > 255) v = 255;
                    storage::mut().ledBrightness = (uint8_t)v;
                    storage::save();
                    leds::reload();
                }
                self->dirty();
            }, /*mask=*/false, Keyboard::StartLayer::Numeric));
            break;
        }
        case TagTest:
            leds::runTest();
            break;
        default: {
            // Colour rows: open keyboard, parse returned hex, update setting.
            uint32_t& field = colorRef(tag);
            LedSettingsScreen* self = this;
            uint8_t captureTag = tag;
            push(new Keyboard("Hex (RRGGBB):", fmtHex(field),
                              [self, captureTag](const String* t) {
                if (t) {
                    uint32_t v = parseHex(*t);
                    colorRef(captureTag) = v;
                    storage::save();
                }
                self->dirty();
            }));
            break;
        }
    }
    dirty();
}

bool LedSettingsScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    if (e.type == EventType::KeyDown && e.key == Key::Left) { pop(); return true; }
    if (e.type == EventType::KeyDown || e.type == EventType::KeyRepeat) {
        if (e.key == Key::Up) {
            cursor_ = (cursor_ - 1 + ROW_COUNT) % ROW_COUNT;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
            dirty(); return true;
        }
        if (e.key == Key::Down) {
            cursor_ = (cursor_ + 1) % ROW_COUNT;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            if (cursor_ >= scrollTop_ + VISIBLE) scrollTop_ = cursor_ - VISIBLE + 1;
            dirty(); return true;
        }
        if ((e.key == Key::Select || e.key == Key::Right) &&
            e.type == EventType::KeyDown) {
            activate(cursor_);
            return true;
        }
    }
    if (e.type == EventType::TouchDown) {
        int row = (e.y - LIST_Y) / ROW_H;
        if (row >= 0 && row < VISIBLE) {
            int i = scrollTop_ + row;
            if (i >= 0 && i < ROW_COUNT) {
                cursor_ = i;
                activate(i);
                return true;
            }
        }
    }
    return false;
}

void LedSettingsScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 34, 240, 286, p.bg);
    tft.setTextFont(2);

    const auto& s = storage::get();
    for (int vi = 0; vi < VISIBLE; ++vi) {
        int i = scrollTop_ + vi;
        if (i >= ROW_COUNT) break;
        int y = LIST_Y + vi * ROW_H;
        bool sel = (i == cursor_);
        uint16_t bg = sel ? p.selBg : p.bg;
        uint16_t fg = sel ? p.selFg : p.textDim;
        tft.fillRect(0, y, 240, ROW_H - 2, bg);

        tft.setTextColor(fg, bg);
        tft.setCursor(6, y + 4);
        tft.print(ROWS[i].label);

        // Right-aligned value display per row kind.
        uint8_t tag = ROWS[i].tag;
        switch (ROWS[i].kind) {
            case Row::Kind::BoolToggle: {
                bool v = s.ledEnabled;
                tft.setTextColor(v ? p.ok : p.textDim, bg);
                tft.setCursor(180, y + 4);
                tft.print(v ? "ON" : "OFF");
                break;
            }
            case Row::Kind::UInt: {
                int v = (tag == TagPin) ? s.ledPin : s.ledBrightness;
                tft.setTextColor(sel ? TFT_YELLOW : p.text, bg);
                tft.setCursor(170, y + 4);
                tft.printf("%d", v);
                break;
            }
            case Row::Kind::Button:
                tft.setTextColor(p.accent, bg);
                tft.setCursor(160, y + 4);
                tft.print("> tap");
                break;
            case Row::Kind::HexColor: {
                uint32_t c = colorRef(tag);
                // Text of the hex.
                tft.setTextColor(sel ? TFT_YELLOW : p.text, bg);
                tft.setCursor(130, y + 4);
                tft.printf("#%06lX", (unsigned long)c);
                // Swatch — 565 conversion (8:8:8 → 5:6:5).
                uint8_t r = (c >> 16) & 0xFF;
                uint8_t g = (c >> 8)  & 0xFF;
                uint8_t b = (c)       & 0xFF;
                uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                tft.fillRect(212, y + 4, 20, 14, rgb565);
                tft.drawRect(212, y + 4, 20, 14, p.text);
                break;
            }
        }
    }

    theme::drawFooter(tft, "SEL=edit  LEFT=back");
}

} // namespace ui
