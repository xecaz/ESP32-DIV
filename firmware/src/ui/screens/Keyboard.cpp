#include "Keyboard.h"

#include <TFT_eSPI.h>
#include <string.h>

#include "../Theme.h"
#include "../UiTask.h"

namespace ui {

namespace {

// Each layout has rows of labels. Multi-character labels (≥2 chars) are
// treated as "special" keys and handled via cellLabel() dispatch.
// Order of rows matters for arrow navigation.
const char* const LOWER_ROWS[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl",
    "^zxcvbnm<",    // ^ = shift toggle, < = backspace
    "#L _  ENT",    // # = layer (num/sym), L = left (home back), _ = space, ENT = enter
    nullptr,
};
const char* const UPPER_ROWS[] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "^ZXCVBNM<",
    "#L _  ENT",
    nullptr,
};
const char* const NUM_ROWS[] = {
    "1234567890",
    "!@#$%^&*()",
    "-_=+[]{};:",
    "@',.?/<",     // < = backspace; @ gets duplicated intentionally
    "ABC _  ENT",  // ABC = back to letters
    nullptr,
};
const char* const SYM_ROWS[] = {
    "~`|\\\"\"'<>",
    "€£¥§±÷×",
    "←→↑↓○●■□",
    "ABC _  ENT",
    nullptr,
};

// Clean numeric-only layout — phone-keypad style. Used for typing
// frequencies, ports, numbers. No letter or symbol layers accessible.
const char* const NUMERIC_ROWS[] = {
    "1 2 3",
    "4 5 6",
    "7 8 9",
    "0 . <",     // < = backspace
    "L ENT",     // L = cancel, ENT = accept
    nullptr,
};

// Keyboard colours are pulled from the active palette at draw time so
// the theme switch reaches this modal too. Special-key accent stays a
// couple shades off the normal key so shift / enter / space still pop.

// For a given row number, return a pre-tokenized list of cell labels.
// Cells are split by " " (space separator in the row source), so rows with
// multi-char tokens like "ENT" or "ABC" work. A literal space character is
// represented by "_" in source (so it can't be confused with the separator).
constexpr int MAX_COLS = 12;

struct ParsedRow {
    const char* cells[MAX_COLS];
    int         count;
};

// Parse a row on demand. Returned pointers are valid for the lifetime of
// `src`; caller owns no memory.
ParsedRow parseRow(const char* src) {
    ParsedRow pr{};
    // Each label is either a single non-space char, or a token terminated by
    // space (e.g. "ENT"). To keep this simple, we treat each *non-space* run
    // as a token; spaces in the source are separators. "_" in the source is
    // rendered as a real space bar.
    // But for simple single-char rows (most of them), every char is a cell.
    // Detect: if the source has a space, we treat it as a multi-token row.
    bool hasSpace = strchr(src, ' ') != nullptr;
    if (!hasSpace) {
        static char single[MAX_COLS][2]; // not re-entrant, but rows parsed
                                         // by the same thread (UI task) only
        int n = 0;
        for (int i = 0; src[i] && n < MAX_COLS; ++i) {
            single[n][0] = src[i];
            single[n][1] = 0;
            pr.cells[n]  = single[n];
            n++;
        }
        pr.count = n;
        return pr;
    }
    // Multi-token row. Tokenize into a static buffer.
    static char  buf[64];
    static char* tokens[MAX_COLS];
    strncpy(buf, src, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int n = 0;
    char* p = buf;
    while (*p && n < MAX_COLS) {
        while (*p == ' ') ++p;
        if (!*p) break;
        tokens[n] = p;
        while (*p && *p != ' ') ++p;
        if (*p) { *p++ = 0; }
        pr.cells[n] = tokens[n];
        n++;
    }
    pr.count = n;
    return pr;
}

} // namespace

Keyboard::Keyboard(const String& prompt, const String& initial, Callback onDone,
                   bool mask, StartLayer startLayer)
    : prompt_(prompt), text_(initial), onDone_(onDone), mask_(mask) {
    switch (startLayer) {
        case StartLayer::Lower:   layer_ = Layer::Lower;   break;
        case StartLayer::Upper:   layer_ = Layer::Upper;   break;
        case StartLayer::Num:     layer_ = Layer::Num;     break;
        case StartLayer::Sym:     layer_ = Layer::Sym;     break;
        case StartLayer::Numeric: layer_ = Layer::Numeric; break;
    }
}

const char* const* Keyboard::rows() const {
    switch (layer_) {
        case Layer::Lower:   return LOWER_ROWS;
        case Layer::Upper:   return UPPER_ROWS;
        case Layer::Num:     return NUM_ROWS;
        case Layer::Sym:     return SYM_ROWS;
        case Layer::Numeric: return NUMERIC_ROWS;
    }
    return LOWER_ROWS;
}

int Keyboard::rowCount() const {
    const char* const* r = rows();
    int n = 0;
    while (r[n]) ++n;
    return n;
}

int Keyboard::colCount(int row) const {
    return parseRow(rows()[row]).count;
}

const char* Keyboard::cellLabel(int row, int col) const {
    ParsedRow pr = parseRow(rows()[row]);
    if (col < 0 || col >= pr.count) return "";
    return pr.cells[col];
}

bool Keyboard::cellIsSpecial(int row, int col) const {
    const char* s = cellLabel(row, col);
    // Multi-char tokens are always special.
    if (strlen(s) > 1) return true;
    // Single-char tokens: ^, <, #, L, _ are specials.
    switch (s[0]) {
        case '^': case '<': case '#': case 'L': case '_': return true;
        default:  return false;
    }
}

char Keyboard::cellChar(int row, int col) const {
    const char* s = cellLabel(row, col);
    if (strlen(s) == 1 && !cellIsSpecial(row, col)) return s[0];
    return 0;
}

void Keyboard::activateCurrentCell() {
    const char* s = cellLabel(cy_, cx_);
    if (!s || !*s) return;

    // Multi-char tokens first.
    if (!strcmp(s, "ENT")) {
        if (onDone_) onDone_(&text_);
        pop();
        return;
    }
    if (!strcmp(s, "ABC")) { layer_ = Layer::Lower; dirty(); return; }

    // Single-char specials.
    if (strlen(s) == 1) {
        char c = s[0];
        switch (c) {
            case '^':  // shift toggle (upper/lower)
                layer_ = (layer_ == Layer::Upper) ? Layer::Lower : Layer::Upper;
                dirty(); return;
            case '#':  // layer switch to numbers/symbols
                layer_ = (layer_ == Layer::Num) ? Layer::Sym : Layer::Num;
                dirty(); return;
            case '<':  // backspace
                if (text_.length()) text_.remove(text_.length() - 1);
                dirty(); return;
            case 'L':  // back (cancel)
                if (onDone_) onDone_(nullptr);
                pop(); return;
            case '_':  // space bar
                text_ += ' ';
                dirty(); return;
            default:
                text_ += c;
                dirty(); return;
        }
    }
}

void Keyboard::onEnter(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillScreen(p.bg);
    tft.fillRect(0, 0, 240, 30, p.headerBg);
    tft.setTextFont(2);
    tft.setTextColor(p.headerFg, p.headerBg);
    tft.setCursor(8, 8);
    tft.print(prompt_);

    // Force a full redraw on first render.
    lastLayer_ = (Layer)0xFF;
    lastCx_ = lastCy_ = -1;
    lastText_ = String(1, '\x01'); // something that won't equal current
    dirty();
}

bool Keyboard::onEvent(const input::Event& e) {
    using input::EventType;
    using input::Key;

    // Touch: hit-test the key grid. A tap both moves the cursor there AND
    // activates the cell (same as pressing SEL on that key).
    if (e.type == EventType::TouchDown) {
        int rows_n = rowCount();
        if (e.y >= KBD_Y && e.y < KBD_Y + rows_n * KEY_H) {
            int row = (e.y - KBD_Y) / KEY_H;
            if (row >= 0 && row < rows_n) {
                int cols_n = colCount(row);
                if (cols_n > 0) {
                    int col = e.x * cols_n / ROW_W;
                    if (col < 0)         col = 0;
                    if (col >= cols_n)   col = cols_n - 1;
                    cy_ = row;
                    cx_ = col;
                    dirty();
                    activateCurrentCell();
                }
            }
        }
        return true;
    }

    if (e.type == EventType::KeyDown || e.type == EventType::KeyRepeat) {
        int rows_n = rowCount();
        switch (e.key) {
            case Key::Up:
                cy_ = (cy_ - 1 + rows_n) % rows_n;
                if (cx_ >= colCount(cy_)) cx_ = colCount(cy_) - 1;
                dirty(); return true;
            case Key::Down:
                cy_ = (cy_ + 1) % rows_n;
                if (cx_ >= colCount(cy_)) cx_ = colCount(cy_) - 1;
                dirty(); return true;
            case Key::Left:
                if (cx_ > 0) { --cx_; dirty(); }
                else {
                    // At column 0: backspace instead of wrapping around —
                    // matches user expectation from mobile keyboards.
                    if (text_.length()) text_.remove(text_.length() - 1);
                    dirty();
                }
                return true;
            case Key::Right: {
                int cols_n = colCount(cy_);
                if (cx_ < cols_n - 1) { ++cx_; dirty(); }
                return true;
            }
            case Key::Select:
                if (e.type == EventType::KeyDown) activateCurrentCell();
                return true;
            default: break;
        }
    }

    // TODO: touch hit-testing will be added in a polish pass; for now the
    // keyboard is fully driven by hardware keys.
    return false;
}

void Keyboard::drawText(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    const int y = 40, h = 30;
    tft.fillRect(0, y, 240, h, p.bg);
    tft.drawRect(4, y + 2, 232, h - 4, p.textDim);
    tft.setTextFont(4);
    tft.setTextColor(p.text, p.bg);
    tft.setCursor(10, y + 6);
    if (mask_) {
        for (size_t i = 0; i < text_.length(); ++i) tft.print('*');
    } else {
        tft.print(text_);
    }
}

void Keyboard::drawKey(TFT_eSPI& tft, int row, int col, bool selected) {
    int cols_n = colCount(row);
    if (cols_n == 0) return;
    int w = ROW_W / cols_n;
    int x = col * w;
    int y = KBD_Y + row * KEY_H;

    const char* s = cellLabel(row, col);
    bool special = cellIsSpecial(row, col);

    const auto& p = theme::palette();
    uint16_t bg = selected ? p.selBg : (special ? p.fieldBg : p.bg);
    uint16_t fg = selected ? p.selFg : p.text;

    tft.fillRect(x + 1, y + 1, w - 2, KEY_H - 2, bg);
    tft.drawRect(x, y, w, KEY_H, p.textDim);

    tft.setTextFont(2);
    tft.setTextColor(fg, bg);
    const char* disp = s;
    if (strlen(s) == 1) {
        switch (s[0]) {
            case '^': disp = "Sh"; break;
            case '<': disp = "<-"; break;
            case '#': disp = (layer_ == Layer::Num || layer_ == Layer::Sym) ? "abc" : "123"; break;
            case 'L': disp = "Esc"; break;
            case '_': disp = "space"; break;
        }
    }
    int tw = tft.textWidth(disp);
    tft.setCursor(x + (w - tw) / 2, y + 8);
    tft.print(disp);
}

void Keyboard::drawAllKeys(TFT_eSPI& tft) {
    int rows_n = rowCount();
    // Clear the keyboard area first so old layer's keys don't leak through.
    tft.fillRect(0, KBD_Y, 240, rows_n * KEY_H + 2, theme::palette().bg);
    for (int r = 0; r < rows_n; ++r) {
        int cols_n = colCount(r);
        for (int c = 0; c < cols_n; ++c) {
            drawKey(tft, r, c, (r == cy_ && c == cx_));
        }
    }
}

void Keyboard::onRender(TFT_eSPI& tft) {
    bool layerChanged = (layer_ != lastLayer_);
    bool textChanged  = (text_ != lastText_);
    bool cursorMoved  = (cy_ != lastCy_ || cx_ != lastCx_);

    if (textChanged || layerChanged) drawText(tft);

    if (layerChanged) {
        drawAllKeys(tft);
    } else if (cursorMoved) {
        // Only redraw the two cells that changed.
        if (lastCx_ >= 0 && lastCy_ >= 0 && lastCy_ < rowCount() &&
            lastCx_ < colCount(lastCy_)) {
            drawKey(tft, lastCy_, lastCx_, false);
        }
        drawKey(tft, cy_, cx_, true);
    }

    lastLayer_ = layer_;
    lastText_  = text_;
    lastCx_    = cx_;
    lastCy_    = cy_;
}

} // namespace ui
