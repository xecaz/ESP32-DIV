#include "WifiSetup.h"

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "Keyboard.h"
#include "../../radio/WifiDriver.h"
#include "../../storage/Settings.h"

namespace ui {

namespace {
constexpr int PICK_Y0      = 56;
constexpr int PICK_ROWH    = 26;
constexpr int PICK_VISIBLE = 8;
}

// ── Async connect worker ──────────────────────────────────────────────
namespace wifi_setup_task {
namespace {
volatile State g_state = State::Idle;
String         g_detail;
TaskHandle_t   g_task  = nullptr;

struct Req {
    String ssid;
    String password;
};

void worker(void* arg) {
    Req* req = (Req*)arg;
    g_state  = State::Busy;
    g_detail = "connecting…";

    bool connected =
        radio::wifi::connectStation(req->ssid, req->password, 10000);
    auto& mut = storage::mut();
    if (!connected) {
        g_state  = State::FailedConn;
        g_detail = "auth/timeout";
        if (mut.staFailCount < 255) mut.staFailCount++;
        storage::save();
        delete req;
        g_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // Reset the boot-time fail counter on success.
    mut.staFailCount = 0;
    storage::save();

    g_detail = "syncing time…";
    if (radio::wifi::ntpSync(8000)) {
        time_t t = time(nullptr);
        struct tm tmv; localtime_r(&t, &tmv);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        g_detail = String(buf);
        g_state  = State::Synced;
    } else {
        g_state  = State::FailedNtp;
        g_detail = "NTP timeout";
    }
    radio::wifi::disconnectStation();
    delete req;
    g_task = nullptr;
    vTaskDelete(nullptr);
}
}

void start(const String& ssid, const String& password) {
    if (g_task) return;  // already running; ignore double-tap
    Req* r = new Req{ssid, password};
    g_state  = State::Busy;
    g_detail = "starting…";
    xTaskCreatePinnedToCore(worker, "wifi_conn", 4096, r, 1, &g_task, 1);
}

State  state()  { return g_state; }
String detail() { return g_detail; }
bool   busy()   { return g_state == State::Busy; }
} // namespace wifi_setup_task

// ── Screen ────────────────────────────────────────────────────────────

void WifiSetupScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Wi-Fi Setup");

    ssid_       = storage::get().staSsid;
    password_   = storage::get().staPassword;
    pickerOpen_ = false;
    dirty();
}

void WifiSetupScreen::onExit(TFT_eSPI&) {
    if (scanStarted_) {
        radio::wifi::stop();
        scanStarted_ = false;
    }
}

void WifiSetupScreen::openPicker() {
    pickerOpen_    = true;
    pickCursor_    = 0;
    pickScroll_    = 0;
    lastScanCount_ = -1;
    if (!scanStarted_) {
        radio::wifi::startScan();
        scanStarted_ = true;
    }
    dirty();
}

void WifiSetupScreen::closePicker() {
    pickerOpen_ = false;
    if (scanStarted_) {
        radio::wifi::stop();
        scanStarted_ = false;
    }
    dirty();
}

bool WifiSetupScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;

    // Picker overlay ----------------------------------------------------
    if (pickerOpen_) {
        int n = radio::wifi::resultCount();
        if (e.type == EventType::KeyDown && e.key == Key::Left) {
            closePicker();
            return true;
        }
        if (e.type == EventType::KeyDown || e.type == EventType::KeyRepeat) {
            if (e.key == Key::Up && n) {
                pickCursor_ = (pickCursor_ - 1 + n) % n;
                if (pickCursor_ < pickScroll_) pickScroll_ = pickCursor_;
                if (pickCursor_ >= pickScroll_ + PICK_VISIBLE)
                    pickScroll_ = pickCursor_ - PICK_VISIBLE + 1;
                dirty();
                return true;
            }
            if (e.key == Key::Down && n) {
                pickCursor_ = (pickCursor_ + 1) % n;
                if (pickCursor_ < pickScroll_) pickScroll_ = pickCursor_;
                if (pickCursor_ >= pickScroll_ + PICK_VISIBLE)
                    pickScroll_ = pickCursor_ - PICK_VISIBLE + 1;
                dirty();
                return true;
            }
            if ((e.key == Key::Select || e.key == Key::Right) &&
                e.type == EventType::KeyDown && n) {
                radio::ScanEntry s;
                if (radio::wifi::resultAt(pickCursor_, s)) {
                    ssid_ = s.ssid;
                    auto& st = storage::mut();
                    st.staSsid      = s.ssid;
                    st.staFailCount = 0;       // user changed creds
                    storage::save();
                }
                closePicker();
                return true;
            }
        }
        if (e.type == EventType::TouchDown && n) {
            int row = (e.y - PICK_Y0) / PICK_ROWH;
            if (row >= 0 && row < PICK_VISIBLE) {
                int i = pickScroll_ + row;
                if (i < n) {
                    radio::ScanEntry s;
                    if (radio::wifi::resultAt(i, s)) {
                        ssid_ = s.ssid;
                        auto& st = storage::mut();
                        st.staSsid      = s.ssid;
                        st.staFailCount = 0;
                        storage::save();
                    }
                    closePicker();
                    return true;
                }
            }
        }
        return false;
    }

    // Setup view --------------------------------------------------------
    if (e.type == EventType::TouchDown) {
        if (e.y >= 50 && e.y <= 78) {
            if (e.x >= 170) {
                openPicker();
                return true;
            }
            WifiSetupScreen* self = this;
            push(new Keyboard("Wi-Fi SSID:", ssid_, [self](const String* t) {
                if (t) {
                    self->ssid_ = *t;
                    auto& s = storage::mut();
                    s.staSsid      = *t;
                    s.staFailCount = 0;
                    storage::save();
                }
                self->dirty();
            }));
            return true;
        }
        if (e.y >= 90 && e.y <= 118) {
            if (e.x >= 186) {
                pwVisible_ = !pwVisible_;
                dirty();
                return true;
            }
            WifiSetupScreen* self = this;
            bool showWhileTyping = pwVisible_;
            push(new Keyboard("Wi-Fi password:", password_, [self](const String* t) {
                if (t) {
                    self->password_ = *t;
                    auto& s = storage::mut();
                    s.staPassword  = *t;
                    s.staFailCount = 0;
                    storage::save();
                }
                self->dirty();
            }, /*mask=*/!showWhileTyping));
            return true;
        }
        if (e.y >= 162 && e.y <= 200) {
            doConnect();
            return true;
        }
    }

    if (e.type == EventType::KeyDown && e.key == Key::Left) {
        pop();
        return true;
    }
    if (e.type == EventType::KeyDown && e.key == Key::Select) {
        doConnect();
        return true;
    }
    return false;
}

void WifiSetupScreen::doConnect() {
    if (wifi_setup_task::busy()) return;
    if (!ssid_.length()) {
        // Surface an immediate error without spawning anything.
        return;
    }
    wifi_setup_task::start(ssid_, password_);
    dirty();
}

void WifiSetupScreen::onTick(uint32_t nowMs) {
    if (pickerOpen_) {
        int n = radio::wifi::resultCount();
        if (n != lastScanCount_) { lastScanCount_ = n; dirty(); }
        radio::wifi::scanDone();
        return;
    }
    // Repaint while a connect task is running so progress is visible; and
    // once per second when synced so the wall-clock line ticks.
    static uint32_t lastRepaintMs = 0;
    if (wifi_setup_task::busy() && nowMs - lastRepaintMs > 200) {
        lastRepaintMs = nowMs;
        dirty();
    }
    if (wifi_setup_task::state() == wifi_setup_task::State::Synced &&
        nowMs - lastRenderMs_ > 1000) {
        lastRenderMs_ = nowMs;
        dirty();
    }
}

void WifiSetupScreen::onRender(TFT_eSPI& tft) {
    const auto& pal = theme::palette();
    tft.fillRect(0, 34, 240, 286, pal.bg);
    tft.setTextFont(2);

    if (pickerOpen_) {
        tft.setTextColor(pal.textDim, pal.bg);
        tft.setCursor(8, 38);
        int n = radio::wifi::resultCount();
        if (radio::wifi::scanRunning())
            tft.printf("scanning...  %d found", n);
        else
            tft.printf("%d nearby APs", n);

        if (n == 0) {
            tft.setTextColor(pal.textDim, pal.bg);
            tft.setCursor(8, PICK_Y0 + 20);
            tft.print("no SSIDs yet...");
        } else {
            for (int vi = 0; vi < PICK_VISIBLE; ++vi) {
                int i = pickScroll_ + vi;
                if (i >= n) break;
                radio::ScanEntry s;
                if (!radio::wifi::resultAt(i, s)) continue;
                int y = PICK_Y0 + vi * PICK_ROWH;
                bool sel = (i == pickCursor_);
                uint16_t bg = sel ? pal.selBg : pal.bg;
                uint16_t fg = sel ? pal.selFg : pal.textDim;
                tft.fillRect(0, y, 240, PICK_ROWH - 2, bg);
                tft.setTextColor(fg, bg);
                tft.setCursor(4, y + 4);
                String ss = s.ssid.length() ? s.ssid : String("<hidden>");
                if (ss.length() > 22) ss = ss.substring(0, 22) + "…";
                tft.print(ss);
                tft.setTextFont(1);
                tft.setTextColor(sel ? TFT_YELLOW : pal.textDim, bg);
                tft.setCursor(180, y + 8);
                tft.printf("%ddB", s.rssi);
                tft.setTextFont(2);
            }
        }

        theme::drawFooter(tft, "SEL=pick  LEFT=cancel");
        return;
    }

    // SSID row (left: keyboard field, right: SCAN button)
    tft.fillRect(2, 50, 160, 28, pal.fieldBg);
    tft.drawRect(2, 50, 160, 28, pal.accent);
    tft.setTextColor(pal.accent, pal.fieldBg);
    tft.setCursor(8, 56);
    String s = ssid_.length() ? ssid_ : String("<tap to type>");
    if (s.length() > 16) s = s.substring(0, 16) + "…";
    tft.printf("SSID: %s", s.c_str());

    tft.fillRect(166, 50, 72, 28, pal.accent);
    tft.drawRect(166, 50, 72, 28, pal.accent);
    tft.setTextColor(pal.selFg, pal.accent);
    tft.setCursor(190, 56);
    tft.print("SCAN");

    // Password row (left: field, right: eye)
    tft.fillRect(2, 90, 180, 28, pal.fieldBg);
    tft.drawRect(2, 90, 180, 28, pal.accent);
    tft.setTextColor(pal.accent, pal.fieldBg);
    tft.setCursor(8, 96);
    String p;
    if (!password_.length()) {
        p = "<tap to set>";
    } else if (pwVisible_) {
        p = password_;
        if (p.length() > 18) p = p.substring(0, 18) + "…";
    } else {
        int n = password_.length(); if (n > 18) n = 18;
        for (int i = 0; i < n; ++i) p += '*';
    }
    tft.printf("PW: %s", p.c_str());

    uint16_t eyeBg = pwVisible_ ? pal.selBg : pal.accent;
    uint16_t eyeFg = pwVisible_ ? pal.ok     : pal.accent;
    tft.fillRect(186, 90, 52, 28, eyeBg);
    tft.drawRect(186, 90, 52, 28, eyeFg);
    int ex = 212, ey = 104;
    tft.drawEllipse(ex, ey, 12, 6, pal.text);
    tft.fillCircle(ex, ey, 3, pwVisible_ ? TFT_YELLOW : pal.text);
    if (!pwVisible_) tft.drawLine(ex - 14, ey - 8, ex + 14, ey + 8, pal.warn);

    // Connect button — disabled look while the worker is running.
    bool busy = wifi_setup_task::busy();
    uint16_t btnBg = busy ? pal.textDim : pal.selBg;
    uint16_t btnBdr = busy ? pal.textDim : pal.ok;
    tft.fillRect(2, 162, 236, 38, btnBg);
    tft.drawRect(2, 162, 236, 38, btnBdr);
    tft.setTextColor(pal.selFg, btnBg);
    tft.setTextFont(4);
    tft.setCursor(48, 170);
    tft.print(busy ? "working..." : "Connect + NTP");
    tft.setTextFont(2);

    // Status line
    tft.setTextColor(pal.textDim, pal.bg);
    tft.setCursor(8, 218);
    using wifi_setup_task::State;
    switch (wifi_setup_task::state()) {
        case State::Idle:
            tft.print("ready"); break;
        case State::Busy:
            tft.setTextColor(TFT_YELLOW, pal.bg);
            tft.print("working..."); break;
        case State::Synced:
            tft.setTextColor(pal.ok, pal.bg);
            tft.print("synced"); break;
        case State::FailedConn:
            tft.setTextColor(pal.warn, pal.bg);
            tft.print("connect failed"); break;
        case State::FailedNtp:
            tft.setTextColor(TFT_ORANGE, pal.bg);
            tft.print("ntp failed"); break;
    }
    tft.setTextColor(pal.textDim, pal.bg);
    tft.setCursor(8, 238);
    String detail = wifi_setup_task::detail();
    if (detail.length() > 30) detail = detail.substring(0, 30) + "…";
    tft.print(detail);

    if (wifi_setup_task::state() == wifi_setup_task::State::Synced) {
        time_t t = time(nullptr);
        struct tm tmv; localtime_r(&t, &tmv);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        tft.setTextColor(pal.textDim, pal.bg);
        tft.setCursor(8, 258);
        tft.printf("now: %s", buf);
    }

    theme::drawFooter(tft, "tap SCAN/eye  SEL=connect  LEFT=back");
}

} // namespace ui
