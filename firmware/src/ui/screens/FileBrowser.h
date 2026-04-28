#pragma once

#include <Arduino.h>
#include "../Screen.h"

namespace ui {

// Tiny SD browser. Shows the contents of a given directory; SELECT on a
// subdir descends, SELECT on a file prompts to delete it. Navigate back
// out with LEFT.
class FileBrowserScreen : public Screen {
public:
    // `path` must start with '/'. Constructs a browser rooted there.
    explicit FileBrowserScreen(const String& path = "/");

    void onEnter(TFT_eSPI& tft) override;
    bool onEvent(const input::Event& e) override;
    void onTick(uint32_t nowMs) override;
    void onRender(TFT_eSPI& tft) override;

private:
    static constexpr int MAX_ENTRIES = 32;

    struct Entry { String name; bool isDir; uint32_t size; };

    String  path_;
    Entry   entries_[MAX_ENTRIES];
    int     count_      = 0;
    int     cursor_     = 0;
    int     scrollTop_  = 0;

    // Pending-delete confirmation: when set, the next SELECT actually
    // removes the file. Reset if user moves the cursor.
    int     confirmingDelete_ = -1;

    // Last status banner ("deleted X", "failed Y", etc).
    String  banner_;
    uint16_t bannerColor_ = 0xFFFF;
    uint32_t bannerMs_    = 0;

    void reload();
};

} // namespace ui
