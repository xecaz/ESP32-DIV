#include "FileBrowser.h"

#include <TFT_eSPI.h>
#include <SD.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../hw/Board.h"

namespace ui {

FileBrowserScreen::FileBrowserScreen(const String& path) : path_(path) {
    if (!path_.length() || path_[0] != '/') path_ = "/";
}

void FileBrowserScreen::reload() {
    count_ = 0;
    cursor_ = 0;
    scrollTop_ = 0;
    confirmingDelete_ = -1;

    if (!board::sdMounted()) return;
    File d = SD.open(path_.c_str());
    if (!d) return;

    while (File f = d.openNextFile()) {
        if (count_ >= MAX_ENTRIES) { f.close(); break; }
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        entries_[count_].name  = name;
        entries_[count_].isDir = f.isDirectory();
        entries_[count_].size  = f.size();
        ++count_;
        f.close();
    }
    d.close();
}

void FileBrowserScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Storage");
    reload();
    dirty();
}

bool FileBrowserScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown && e.type != EventType::KeyRepeat) return false;

    switch (e.key) {
        case Key::Left:
            if (e.type != EventType::KeyDown) return true;
            if (path_ == "/") { pop(); return true; }
            // Go up one level.
            {
                int slash = path_.lastIndexOf('/');
                path_ = (slash <= 0) ? String("/") : path_.substring(0, slash);
            }
            reload();
            dirty();
            return true;
        case Key::Up:
            if (!count_) return true;
            cursor_ = (cursor_ - 1 + count_) % count_;
            if (cursor_ < scrollTop_) scrollTop_ = cursor_;
            confirmingDelete_ = -1;
            dirty();
            return true;
        case Key::Down:
            if (!count_) return true;
            cursor_ = (cursor_ + 1) % count_;
            if (cursor_ >= scrollTop_ + 8) scrollTop_ = cursor_ - 7;
            confirmingDelete_ = -1;
            dirty();
            return true;
        case Key::Select: case Key::Right: {
            if (e.type != EventType::KeyDown || !count_) return true;
            auto& ent = entries_[cursor_];
            if (ent.isDir) {
                path_ = path_ == "/" ? String("/") + ent.name
                                     : path_ + "/" + ent.name;
                reload();
                dirty();
                return true;
            }
            // File: two-press delete. First SEL primes; second deletes.
            if (confirmingDelete_ != cursor_) {
                confirmingDelete_ = cursor_;
                banner_ = "press SEL again to delete";
                bannerColor_ = TFT_ORANGE;
                bannerMs_ = millis();
                dirty();
                return true;
            }
            String full = (path_ == "/") ? String("/") + ent.name
                                         : path_ + "/" + ent.name;
            bool ok = SD.remove(full.c_str());
            banner_ = ok ? String("deleted ") + ent.name
                         : String("delete failed: ") + ent.name;
            bannerColor_ = ok ? theme::palette().ok : theme::palette().warn;
            bannerMs_ = millis();
            confirmingDelete_ = -1;
            reload();
            dirty();
            return true;
        }
        default: return false;
    }
}

void FileBrowserScreen::onTick(uint32_t nowMs) {
    if (banner_.length() && nowMs - bannerMs_ > 2000) {
        banner_ = "";
        dirty();
    }
}

void FileBrowserScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, 30, 240, 290, p.bg);

    tft.setTextFont(2);
    tft.setTextColor(p.textDim, p.bg);
    tft.setCursor(8, 34);
    if (!board::sdMounted()) {
        tft.setTextColor(p.warn, p.bg);
        tft.print("SD not mounted — insert card");
        theme::drawFooter(tft, "LEFT=back");
        return;
    }

    tft.printf("dir: %s", path_.c_str());

    if (!count_) {
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 70);
        tft.print("(empty)");
    } else {
        constexpr int ROW_H = 24;
        for (int vi = 0; vi < 8; ++vi) {
            int i = scrollTop_ + vi;
            if (i >= count_) break;
            int y = 56 + vi * ROW_H;
            bool sel = (i == cursor_);
            bool primed = (i == confirmingDelete_);
            uint16_t bg = primed ? p.warn
                                 : (sel ? p.selBg : p.bg);
            uint16_t fg = sel ? p.selFg : p.textDim;
            tft.fillRect(0, y, 240, ROW_H - 2, bg);
            tft.setTextFont(2);
            tft.setTextColor(fg, bg);
            tft.setCursor(8, y + 2);
            String name = entries_[i].name;
            if (entries_[i].isDir) name = String("/") + name;
            if (name.length() > 22) name = name.substring(0, 22) + "…";
            tft.print(name);
            tft.setTextFont(1);
            tft.setTextColor(sel ? TFT_YELLOW : p.textDim, bg);
            tft.setCursor(180, y + 6);
            if (entries_[i].isDir) tft.print("DIR");
            else if (entries_[i].size < 1024) tft.printf("%lu B",
                                                        (unsigned long)entries_[i].size);
            else                             tft.printf("%lu KB",
                                                        (unsigned long)entries_[i].size / 1024);
        }
    }

    if (banner_.length() && millis() - bannerMs_ < 2000) {
        tft.setTextFont(2);
        tft.setTextColor(bannerColor_, p.bg);
        tft.setCursor(8, 280);
        tft.print(banner_);
    }

    theme::drawFooter(tft, "SEL=open/delete  LEFT=up");
}

} // namespace ui
