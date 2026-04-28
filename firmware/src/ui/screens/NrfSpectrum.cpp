#include "NrfSpectrum.h"

#include <TFT_eSPI.h>

#include "../Theme.h"
#include "../UiTask.h"
#include "../../radio/Nrf24Driver.h"

namespace ui {

namespace {
constexpr int GRAPH_Y = 40;
constexpr int GRAPH_H = 240;
constexpr int GRAPH_W = 240;
constexpr int N       = radio::nrf24::SPECTRUM_CHANNELS;   // 84 → ~2.86 px/ch
}

void NrfSpectrumScreen::onEnter(TFT_eSPI& tft) {
    tft.fillScreen(theme::palette().bg);
    theme::drawHeader(tft, "2.4GHz Spec");

    memset(levels_, 0, sizeof(levels_));
    radio::nrf24::startSpectrum();
    dirty();
}

void NrfSpectrumScreen::onExit(TFT_eSPI&) {
    radio::nrf24::stopSpectrum();
}

bool NrfSpectrumScreen::onEvent(const input::Event& e) {
    using input::EventType; using input::Key;
    if (e.type != EventType::KeyDown) return false;
    if (e.key == Key::Left) { pop(); return true; }
    if (e.key == Key::Select || e.key == Key::Right) {
        memset(levels_, 0, sizeof(levels_));
        dirty();
        return true;
    }
    return false;
}

void NrfSpectrumScreen::onTick(uint32_t nowMs) {
    if (nowMs - lastSampleMs_ < 50) return;
    lastSampleMs_ = nowMs;

    uint8_t snap[N];
    if (!radio::nrf24::spectrumSnapshot(snap)) return;

    // Exponential smoothing: hits bump up fast, quiet fades slowly.
    for (int i = 0; i < N; ++i) {
        if (snap[i]) levels_[i] = 255;
        else         levels_[i] = (uint8_t)(levels_[i] * 7 / 8);
    }
    dirty();
}

void NrfSpectrumScreen::onRender(TFT_eSPI& tft) {
    const auto& p = theme::palette();
    tft.fillRect(0, GRAPH_Y, GRAPH_W, GRAPH_H, p.bg);

    // Spread N channels (the 2.4 GHz ISM band) across the full width.
    // Each channel gets ~2.86 px which is wide enough to be visible and
    // narrow enough that 84 channels still fit cleanly.
    for (int ch = 0; ch < N; ++ch) {
        int x0 = (ch * GRAPH_W) / N;
        int x1 = ((ch + 1) * GRAPH_W) / N;
        int h  = (levels_[ch] * GRAPH_H) / 256;
        if (h < 1) continue;
        uint16_t color = levels_[ch] > 200 ? p.warn
                       : levels_[ch] > 100 ? TFT_ORANGE
                                           : p.ok;
        tft.fillRect(x0, GRAPH_Y + GRAPH_H - h, x1 - x0, h, color);
    }

    // Axis hints. WiFi 2.4 GHz channels live every 5 NRF channels starting
    // at NRF ch 2. BLE adv channels are 37/38/39 → NRF 2/26/80.
    tft.setTextFont(1);
    tft.setTextColor(p.textDim, p.bg);
    auto markerX = [](int nrfCh) { return (nrfCh * GRAPH_W) / N; };
    for (int wifiCh = 1; wifiCh <= 13; wifiCh += 1) {
        int nrfCh = (wifiCh - 1) * 5 + 2;
        if (nrfCh >= N) break;
        int x = markerX(nrfCh);
        // Tick mark + label on every WiFi channel; label only on 1/6/11 to
        // avoid overlapping text.
        tft.drawFastVLine(x, GRAPH_Y + GRAPH_H, 3, p.textDim);
        if (wifiCh == 1 || wifiCh == 6 || wifiCh == 11) {
            tft.setCursor(x - 3, GRAPH_Y + GRAPH_H + 4);
            tft.printf("%d", wifiCh);
        }
    }
    // BLE advertising markers in cyan so they stand out from WiFi labels.
    tft.setTextColor(p.accent, p.bg);
    for (int nrfCh : { 2, 26, 80 }) {
        if (nrfCh >= N) continue;
        int x = markerX(nrfCh);
        tft.drawFastVLine(x, GRAPH_Y, 4, p.accent);
    }
    tft.setCursor(2, GRAPH_Y - 10);
    tft.print("BLE adv");

    theme::drawFooter(tft, "SEL = clear   LEFT = back");
}

} // namespace ui
