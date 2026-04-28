#pragma once

#include <Arduino.h>
#include <functional>

#include "../Screen.h"

namespace ui {

// Modal on-screen keyboard. Replaces the broken stock keyboard:
//   - four layers (lowercase, UPPERCASE, numbers, symbols) with a working
//     space bar — fixes the wifi-password-entry bug
//   - operable by hardware arrow keys *and* touch, interchangeably
//   - non-blocking: pushes events into the UiTask queue like any screen
//
// Usage: push a Keyboard, provide a callback. When the user presses Enter,
// the callback is invoked with the entered text; the keyboard pops itself.
// Pressing LEFT while the text is empty cancels (callback called with null).
class Keyboard : public Screen {
public:
    using Callback = std::function<void(const String* textOrNullIfCanceled)>;

    // Numeric = phone-keypad style digits + ".", no letter or symbol
    // layers. Used for entering frequencies, port numbers, etc.
    enum class StartLayer : uint8_t { Lower, Upper, Num, Sym, Numeric };

    Keyboard(const String& prompt, const String& initial, Callback onDone,
             bool mask = false, StartLayer startLayer = StartLayer::Lower);

    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    enum class Layer : uint8_t { Lower = 0, Upper, Num, Sym, Numeric };

    String   prompt_;
    String   text_;
    Callback onDone_;
    bool     mask_;

    Layer    layer_ = Layer::Lower;
    int      cx_    = 0;  // cursor column
    int      cy_    = 0;  // cursor row

    int      lastCx_ = -1;
    int      lastCy_ = -1;
    Layer    lastLayer_ = Layer::Num;  // force full paint on first render
    String   lastText_;

    // Layout helpers.
    const char* const* rows() const;
    int rowCount() const;
    int colCount(int row) const;
    char cellChar(int row, int col) const;
    const char* cellLabel(int row, int col) const;
    bool cellIsSpecial(int row, int col) const;

    void activateCurrentCell();
    void drawText(TFT_eSPI& tft);
    void drawKey(TFT_eSPI& tft, int row, int col, bool selected);
    void drawAllKeys(TFT_eSPI& tft);

    static constexpr int KBD_Y = 150;    // top of key grid
    static constexpr int KEY_H = 32;
    static constexpr int ROW_W = 240;
};

} // namespace ui
