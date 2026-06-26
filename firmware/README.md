# ESP32-DIV firmware

Independent, ground-up firmware for an ESP32-S3 "ESP32-DIV" wireless multitool
(WiFi / BLE / 2.4 GHz NRF24 / sub-GHz CC1101 / IR / USB-HID). PlatformIO,
Arduino-on-IDF, FreeRTOS task architecture with a touch + D-pad UI.

This is a clean reimplementation of the CiferTech ESP32-DIV concept — not a fork
of the vendor's contractor firmware. It adds reliable input debounce, WiFi
station setup, a real voltage-based battery gauge, and shared-SPI bus
arbitration that the stock firmware lacks.

## Build & flash

```
cd firmware
.venv/bin/pio run -e esp32div            # build
.venv/bin/pio run -e esp32div -t upload  # flash over USB (/dev/ttyACM0)
```

This board revision flashes over the ESP32-S3 built-in USB-Serial/JTAG — no
manual boot-mode entry needed. There is no serial console on this revision; use
the on-device **I²C Health** screen for diagnostics.

## Hardware & architecture

See [`CLAUDE.md`](CLAUDE.md) for the board's bus map, pin assignments, and the
reasoning behind them, and [`../PLAN.md`](../PLAN.md) for roadmap/history.
GPIO assignments live in [`src/hw/Pins.h`](src/hw/Pins.h) (single source of
truth).

## Status

Active branch `stock-pins-battery`: moving from old-board hardware bodges to the
stock board (correct R30/R31 I²C pull-ups), and adding a GPIO2-ADC battery
monitor. See the "Current work" section of `CLAUDE.md`.
