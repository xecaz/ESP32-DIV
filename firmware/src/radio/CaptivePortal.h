#pragma once

#include <Arduino.h>

namespace radio {
namespace captive {

// Brings up a WiFi AP, a DNS "catch-all" server, and an HTTP server. Any
// credential submitted is stored in the in-memory log that the UI polls.
//
// Content resolution:
//   1. If the SD card has a `/portal/` directory with an index.html (and
//      optional css/js/images), those files are served — GET requests
//      map to `/portal/<uri>` with `/` redirected to `/portal/index.html`.
//   2. Otherwise the built-in inline landing form is used.
//
// The same rule applies to captive-portal detection URLs so OS "sign-in"
// popups land on the custom page when present.
bool start(const String& ssid);
void stop();
bool running();

// Most recent credential submission (captured from POST). Empty if none
// seen yet.
struct Submission { String user; String pass; String clientIp; uint32_t ms; };
int         submissionCount();
Submission  submissionAt(int i);
int         clientCount();

} // namespace captive
} // namespace radio
