#pragma once

#include <Arduino.h>

namespace usb {
namespace ducky {

constexpr const char* SCRIPT_DIR = "/rubberducky";

// List `*.txt` scripts in SCRIPT_DIR. Returns the count; fills up to `max`
// basenames into `out`.
int listScripts(String* out, int max);

// Arm a script to run on the next host-connect transition. `basename`
// refers to a file in SCRIPT_DIR (e.g. "hello.txt").
bool arm(const String& basename);
void disarm();
bool armed();
const String& armedName();

// Check arm/host state and fire if both ready. Call periodically from
// a screen or the main loop; no-op when not armed or host not connected.
// Returns true if a run just started (script is executing in the caller).
bool maybeRun();

// Immediate run — executes the currently armed script (or `path` if given).
// Blocks while typing. Returns true on successful parse + run.
bool runNow();
bool runFile(const String& path);

// Last run status — surfaced on the Ducky screen.
uint32_t lastRunMs();
int      lastRunLines();
} // namespace ducky
} // namespace usb
