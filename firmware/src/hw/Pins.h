#pragma once

// ESP32-DIV v2 board pin map. Single source of truth.
// Sourced from cifertech's shared.h + TFT_eSPI User_Setup.h (bundled lib).
// Any driver that needs a GPIO reads it from here.

#include <stdint.h>

namespace pins {

// ── Display (ILI9341 over HSPI) ─────────────────────────────────────────
// TFT_eSPI reads its pins from -DTFT_* macros in platformio.ini, so we
// name ours differently to avoid macro-substitution collisions.
constexpr int DISPLAY_MISO      = 37;
constexpr int DISPLAY_MOSI      = 35;
constexpr int DISPLAY_SCLK      = 36;
constexpr int DISPLAY_CS        = 17;
constexpr int DISPLAY_DC        = 16;
constexpr int DISPLAY_RST       = 0;   // GPIO0 is strap/boot; must be high at reset
constexpr int DISPLAY_BACKLIGHT = 7;   // PWM via LEDC, not TFT_eSPI

// ── Touch (XPT2046 over HSPI, shares bus pins with display) ─────────────
constexpr int TOUCH_MISO_PIN = 37;
constexpr int TOUCH_MOSI_PIN = 35;
constexpr int TOUCH_SCLK_PIN = 36;
constexpr int TOUCH_CS_PIN   = 18;

// ── Buttons via PCF8574 I²C expander ────────────────────────────────────
// Stock wiring: the PCF8574T (0x20) and the IP5306 battery PMIC (0x75)
// share ONE I²C bus on GPIO 8 (SDA) / GPIO 9 (SCL), pulled up by R30/R31
// (1k each, per schematic). The old unit's flaky reads / 1-second bus
// stalls traced to missing/faulty R30/R31 — and were bodged around on
// that board (SDA/SCL → GPIO 41/42, PCF INT → GPIO 2). This revision has
// correct R30/R31, so we run the stock bus with NO bodges. That also frees
// GPIO 2 for its real owners: the buzzer and the VBAT/2 battery sense.
constexpr uint8_t PCF_I2C_ADDR = 0x20;
constexpr int     I2C_SDA      = 8;    // stock SDA (shared with IP5306 0x75)
constexpr int     I2C_SCL      = 9;    // stock SCL (shared with IP5306 0x75)
// These are PCF8574 bit indices, NOT GPIO numbers.
constexpr int BTN_UP     = 7;
constexpr int BTN_DOWN   = 5;
constexpr int BTN_LEFT   = 3;
constexpr int BTN_RIGHT  = 4;
constexpr int BTN_SELECT = 6;

// ── Shared SPI bus: SD / CC1101 / NRF24 all on these pins ───────────────
constexpr int SPI_MOSI = 11;
constexpr int SPI_MISO = 13;
constexpr int SPI_SCLK = 12;

// ── SD card ─────────────────────────────────────────────────────────────
constexpr int SD_CS = 10;
constexpr int SD_CD = 38; // card-detect

// ── CC1101 sub-GHz ──────────────────────────────────────────────────────
constexpr int CC1101_CS   = 5;
constexpr int CC1101_GDO0 = 6;  // TX/data out
constexpr int CC1101_GDO2 = 3;  // RX/data in

// ── NRF24L01+ ×3 ────────────────────────────────────────────────────────
constexpr int NRF24_1_CE  = 15;
constexpr int NRF24_1_CSN = 4;
constexpr int NRF24_2_CE  = 47;
constexpr int NRF24_2_CSN = 48;
// NRF24 #3 conflicts with IR pins — see ShieldProfile runtime setting.
constexpr int NRF24_3_CE  = 14;
constexpr int NRF24_3_CSN = 21;

// ── IR (default pins overlap with NRF24 #3) ─────────────────────────────
constexpr int IR_TX = 14;
constexpr int IR_RX = 21;

// ── Misc ────────────────────────────────────────────────────────────────
constexpr int BUZZER      = 2;
constexpr int BATTERY_ADC = 34;

} // namespace pins
