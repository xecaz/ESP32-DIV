#include "DuckyRunner.h"

#include <Arduino.h>
#include <SD.h>
#include <USBHIDKeyboard.h>

#include "UsbHostWatch.h"
#include "../hw/Board.h"

namespace usb {
namespace ducky {

namespace {

// Borrow the HID keyboard instance. UsbHid.cpp created its own; rather
// than plumb a reference through, we create a fresh instance — USB HID
// allows multiple keyboards on the same composite device and TinyUSB
// multiplexes reports by collection.
USBHIDKeyboard g_kb;
bool           g_kbStarted = false;

String   g_armedBase;
bool     g_armed          = false;
bool     g_running        = false;
uint32_t g_prevHostAt     = 0;
uint32_t g_lastRunMs      = 0;
int      g_lastRunLines   = 0;

void ensureKb() {
    if (g_kbStarted) return;
    g_kb.begin();
    g_kbStarted = true;
}

// Map a keyword to a HID key code (subset of classic DuckyScript).
uint8_t keyFor(const String& token) {
    String t = token;
    t.toUpperCase();
    if (t == "ENTER" || t == "RETURN")     return KEY_RETURN;
    if (t == "TAB")                        return KEY_TAB;
    if (t == "SPACE")                      return ' ';
    if (t == "ESC" || t == "ESCAPE")       return KEY_ESC;
    if (t == "BACKSPACE")                  return KEY_BACKSPACE;
    if (t == "DELETE" || t == "DEL")       return KEY_DELETE;
    if (t == "INSERT")                     return KEY_INSERT;
    if (t == "UP" || t == "UPARROW")       return KEY_UP_ARROW;
    if (t == "DOWN" || t == "DOWNARROW")   return KEY_DOWN_ARROW;
    if (t == "LEFT" || t == "LEFTARROW")   return KEY_LEFT_ARROW;
    if (t == "RIGHT" || t == "RIGHTARROW") return KEY_RIGHT_ARROW;
    if (t == "HOME")                       return KEY_HOME;
    if (t == "END")                        return KEY_END;
    if (t == "PAGEUP")                     return KEY_PAGE_UP;
    if (t == "PAGEDOWN")                   return KEY_PAGE_DOWN;
    if (t == "CAPSLOCK")                   return KEY_CAPS_LOCK;
    if (t.startsWith("F") && t.length() <= 3) {
        int n = t.substring(1).toInt();
        if (n >= 1 && n <= 12) return KEY_F1 + (n - 1);
    }
    if (t.length() == 1) return (uint8_t)t[0];
    return 0;
}

uint8_t modifierFor(const String& token) {
    String t = token; t.toUpperCase();
    if (t == "CTRL"  || t == "CONTROL") return KEY_LEFT_CTRL;
    if (t == "SHIFT")                   return KEY_LEFT_SHIFT;
    if (t == "ALT")                     return KEY_LEFT_ALT;
    if (t == "GUI"   || t == "WINDOWS" || t == "CMD") return KEY_LEFT_GUI;
    return 0;
}

void runLine(const String& raw) {
    String line = raw;
    line.trim();
    if (!line.length() || line.startsWith("REM") || line.startsWith("#")) return;

    // Split off first word as the command.
    int sp = line.indexOf(' ');
    String cmd = (sp < 0) ? line : line.substring(0, sp);
    String rest = (sp < 0) ? String() : line.substring(sp + 1);
    cmd.trim(); rest.trim();
    String cmdUp = cmd; cmdUp.toUpperCase();

    if (cmdUp == "STRING" || cmdUp == "STRINGLN") {
        for (size_t i = 0; i < rest.length(); ++i) g_kb.write(rest[i]);
        if (cmdUp == "STRINGLN") g_kb.press(KEY_RETURN);
        g_kb.releaseAll();
        return;
    }
    if (cmdUp == "DELAY" || cmdUp == "SLEEP") {
        int ms = rest.toInt();
        if (ms < 1)    ms = 1;
        if (ms > 60000) ms = 60000;
        delay(ms);
        return;
    }

    // First pass: tokenize and categorize. If the line contains any
    // modifier (GUI/CTRL/ALT/SHIFT) OR looks like a bare special-key
    // word on its own (ENTER, TAB, F5, …), treat the whole line as a
    // DuckyScript chord. Otherwise, type it literally — lets the user
    // drop shell one-liners like `echo hacked > ~/f.txt` straight into
    // /rubberducky/ without wrapping every line in STRING.
    bool hasModifier = false;
    bool hasNamedKey = false;  // multi-char keyword like ENTER, TAB, F5
    {
        int s2 = 0;
        while (s2 < (int)line.length()) {
            int sp2 = line.indexOf(' ', s2);
            if (sp2 < 0) sp2 = line.length();
            String tok = line.substring(s2, sp2);
            tok.trim();
            if (tok.length()) {
                if (modifierFor(tok))            hasModifier = true;
                else if (tok.length() > 1 && keyFor(tok)) hasNamedKey = true;
            }
            s2 = sp2 + 1;
        }
    }

    if (hasModifier || (hasNamedKey && !hasModifier)) {
        // Chord / keyword: press all modifiers, then the key, then release.
        int s3 = 0;
        while (s3 < (int)line.length()) {
            int sp3 = line.indexOf(' ', s3);
            if (sp3 < 0) sp3 = line.length();
            String tok = line.substring(s3, sp3);
            tok.trim();
            if (tok.length()) {
                uint8_t m = modifierFor(tok);
                if (m) g_kb.press(m);
                else {
                    uint8_t k = keyFor(tok);
                    if (k) g_kb.press(k);
                }
            }
            s3 = sp3 + 1;
        }
        delay(10);
        g_kb.releaseAll();
    } else {
        // Bare text / shell line — type as-is.
        for (size_t i = 0; i < line.length(); ++i) g_kb.write(line[i]);
        g_kb.releaseAll();
    }
}

} // namespace

int listScripts(String* out, int max) {
    if (!board::sdMounted()) return 0;
    if (!SD.exists(SCRIPT_DIR)) SD.mkdir(SCRIPT_DIR);
    File d = SD.open(SCRIPT_DIR);
    if (!d) return 0;
    int n = 0;
    while (File f = d.openNextFile()) {
        if (!f.isDirectory()) {
            String name = f.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            // Accept any non-hidden file — .txt / .bat / .sh / .duck / .ps1
            // all welcome. User decides what content looks like; the parser
            // will just try to interpret every line regardless.
            if (name.length() && name[0] != '.') {
                if (out && n < max) out[n] = name;
                ++n;
            }
        }
        f.close();
    }
    d.close();
    return n;
}

bool arm(const String& basename) {
    if (!board::sdMounted()) return false;
    g_armedBase = basename;
    g_armed = true;
    // Remember the current hostConnectedAt so maybeRun() only fires on the
    // *next* connect transition, not the one that's already active.
    g_prevHostAt = usb::hostConnectedAtMs();
    return true;
}

void disarm() { g_armed = false; g_armedBase = ""; }
bool armed()  { return g_armed; }
const String& armedName() { return g_armedBase; }

bool maybeRun() {
    if (!g_armed || g_running) return false;
    if (!usb::hostConnected()) return false;
    uint32_t at = usb::hostConnectedAtMs();
    if (at == 0 || at == g_prevHostAt) return false;   // same session
    // Give the host ~1.5 s to fully enumerate the HID before spraying keys.
    if (millis() - at < 1500) return false;
    g_armed = false;
    runNow();
    return true;
}

bool runNow() {
    if (!g_armedBase.length()) return false;
    String path = String(SCRIPT_DIR) + "/" + g_armedBase;
    bool ok = runFile(path);
    // Immediate run consumes the arm — don't want it firing again on the
    // next unrelated host-connect.
    g_armed = false;
    return ok;
}

bool runFile(const String& path) {
    if (!board::sdMounted()) return false;
    ensureKb();
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    g_running = true;
    g_lastRunLines = 0;
    String line;
    line.reserve(96);
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\r') continue;
        if (c == '\n') {
            runLine(line);
            ++g_lastRunLines;
            line = "";
        } else {
            line += c;
            if (line.length() > 512) { runLine(line); line = ""; }
        }
    }
    if (line.length()) { runLine(line); ++g_lastRunLines; }
    f.close();
    g_lastRunMs = millis();
    g_running = false;
    return true;
}

uint32_t lastRunMs()    { return g_lastRunMs; }
int      lastRunLines() { return g_lastRunLines; }

} // namespace ducky
} // namespace usb
