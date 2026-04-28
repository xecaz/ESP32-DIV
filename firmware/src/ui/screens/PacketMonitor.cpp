#include "PacketMonitor.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/WifiCapture.h"
#include "../../hw/Board.h"

namespace ui {

void PacketMonitorScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "Packet Mon");

    startedMs_    = millis();
    staticDrawn_  = false;
    lastPeak_     = 0;
    memset(lastRate_, 0, sizeof(lastRate_));
    if (!board::sdMounted()) board::mountSd();
    radio::wifi_cap::start();
    dirty();
}

void PacketMonitorScreen::onExit(TFT_eSPI&) {
    radio::wifi_cap::stop();
}

bool PacketMonitorScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    if (e.key == Key::Left) { pop(); return true; }
    return false;
}

void PacketMonitorScreen::onTick(uint32_t nowMs) {
    uint32_t p = radio::wifi_cap::packetsCaptured();
    uint32_t b = radio::wifi_cap::bytesWritten();
    if (p != lastPkts_ || b != lastBytes_) {
        lastPkts_ = p; lastBytes_ = b; dirty();
    }
    // Redraw the graph once per second so bars advance even during lulls.
    static uint32_t lastTickMs = 0;
    if (nowMs - lastTickMs >= 1000) { lastTickMs = nowMs; dirty(); }
}

void PacketMonitorScreen::onRender(TFT_eSPI& tft) {
    constexpr int GX = 8, GY = 160, GW = 224, GH = 100;
    constexpr int VALUE_X = 56;   // "label: " is 7 chars ≈ 48 px
    constexpr int F2_H    = 18;

    const auto& p = theme::palette();
    tft.setTextFont(2);

    // SD-missing path owns the whole body — just repaint fully, it's
    // static anyway and doesn't flicker because we only hit it when the
    // card is out.
    if (!board::sdMounted()) {
        tft.fillRect(0, 40, 240, 280, p.bg);
        tft.setTextColor(p.warn, p.bg);
        tft.setCursor(8, 60); tft.print("SD not mounted!");
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8, 80); tft.print("insert card and re-enter.");
        theme::drawFooter(tft, "LEFT = back");
        staticDrawn_ = false;
        return;
    }

    // One-time: labels, filename, graph border, footer hint.
    if (!staticDrawn_) {
        tft.fillRect(0, 40, 240, 280, p.bg);

        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(8,  98); tft.print("pkts:");
        tft.setCursor(8, 116); tft.print("bytes:");
        tft.setCursor(8, 134); tft.print("up:");

        String f = radio::wifi_cap::currentFilename();
        int slash = f.lastIndexOf('/');
        if (slash >= 0) f = f.substring(slash + 1);
        if (f.length() > 30) f = f.substring(0, 30) + "…";
        tft.setCursor(8, 74); tft.print(f);

        tft.drawRect(GX, GY, GW, GH, p.textDim);
        tft.setTextFont(1);
        tft.setTextColor(p.textDim, p.bg);
        tft.setCursor(GX, GY - 10);
        tft.print("pkts/s  last 60s");

        tft.setCursor(8, 306); tft.print("LEFT = stop + back");
        tft.setTextFont(2);
        staticDrawn_ = true;
    }

    // Dynamic "CAPTURING/idle" — its colour can change.
    tft.fillRect(8, 50, 140, F2_H, p.bg);
    tft.setTextColor(radio::wifi_cap::running() ? p.ok : p.textDim,
                     p.bg);
    tft.setCursor(8, 50);
    tft.print(radio::wifi_cap::running() ? "CAPTURING" : "idle");

    // Dynamic numeric values: wipe just the value rectangle, then reprint.
    auto line = [&](int y, const char* fmt, uint32_t v) {
        tft.fillRect(VALUE_X, y, 240 - VALUE_X, F2_H, p.bg);
        tft.setTextColor(p.text, p.bg);
        tft.setCursor(VALUE_X, y);
        tft.printf(fmt, (unsigned long)v);
    };
    line(98,  "%lu",  radio::wifi_cap::packetsCaptured());
    line(116, "%lu",  radio::wifi_cap::bytesWritten());

    // "up:" shows two fields: elapsed + peak.
    uint32_t elapsed = millis() - startedMs_;
    tft.fillRect(VALUE_X, 134, 240 - VALUE_X, F2_H, p.bg);
    tft.setTextColor(p.text, p.bg);
    tft.setCursor(VALUE_X, 134);
    tft.printf("%lus   peak %u/s",
               (unsigned long)(elapsed / 1000),
               radio::wifi_cap::peakPacketsPerSec());

    // Graph: only update columns whose value changed (and rescale if peak
    // changed). Avoids the full-fill-every-second flicker.
    uint16_t hist[radio::wifi_cap::RATE_HISTORY_LEN];
    radio::wifi_cap::rateHistory(hist);
    uint16_t peak = radio::wifi_cap::peakPacketsPerSec();
    if (peak < 4) peak = 4;

    int cols = radio::wifi_cap::RATE_HISTORY_LEN;
    int colW = (GW - 2) / cols;
    if (colW < 1) colW = 1;
    bool peakChanged = (peak != lastPeak_);
    for (int i = 0; i < cols; ++i) {
        if (!peakChanged && hist[i] == lastRate_[i]) continue;
        int x = GX + 1 + i * colW;
        int innerH = GH - 2;
        // Clear this column's interior first.
        tft.fillRect(x, GY + 1, colW, innerH, p.bg);
        int h = (int)hist[i] * innerH / peak;
        if (h > innerH) h = innerH;
        int y = GY + GH - 1 - h;
        int pct = (int)hist[i] * 100 / peak;
        uint16_t c = pct < 25 ? p.ok :
                     pct < 75 ? TFT_ORANGE : p.warn;
        if (h > 0) tft.fillRect(x, y, colW, h, c);
        lastRate_[i] = hist[i];
    }
    lastPeak_ = peak;
}

} // namespace ui
