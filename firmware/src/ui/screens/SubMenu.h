#pragma once

#include "../Screen.h"

namespace ui {

// Generic placeholder sub-menu. Replaced one-by-one in M5/M6 as each feature
// gets a real screen. For now, SubMenu shows a back-able list with an
// "unimplemented" note per item so M3 navigation can be exercised.
class SubMenu : public Screen {
public:
    explicit SubMenu(const char* categoryName);

    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onRender(TFT_eSPI& tft) override;

private:
    const char* category_;
    int         cursor_     = 0;
    int         lastCursor_ = -1;
    int         itemCount_  = 0;

    const char* const* items_ = nullptr;

    void resolveItems();
};

} // namespace ui
