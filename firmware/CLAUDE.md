# CLAUDE.md — ESP32-DIV firmware

Guidance for Claude Code / future sessions. Read this before touching pins,
buses, or USB config — most of it was hard-won and is NOT obvious from the code.

## What this is

Independent, ground-up firmware for an ESP32-S3 "ESP32-DIV" wireless multitool
(CiferTech-derived hardware). PlatformIO, Arduino-on-IDF, ~14k LOC, FreeRTOS
task architecture with a `Screen`-based UI. This is **our** firmware — NOT the
vendor's. The vendor ships a contractor's binary (a feature-stripped build of
upstream CiferTech "Cirket"); none of our code is in it. See `../wtf/` for that
analysis.

## Build / flash / debug

```
cd firmware
.venv/bin/pio run -e esp32div            # build
.venv/bin/pio run -e esp32div -t upload  # flash (port /dev/ttyACM0)
```

- Board: `esp32-s3-devkitc-1`, 16 MB flash, **no PSRAM**.
- **No serial console on this board** (see USB note). Use the on-device
  **I²C Health** screen for diagnostics, not `pio device monitor`.

## Hardware truths (verify against `../ESP32-DIV/Schematic/` before pin changes)

Single source of truth for GPIO is `src/hw/Pins.h`. Two buses — **do not
conflate them**:

- **I²C — GPIO 8 (SDA) / 9 (SCL).** PCF8574 buttons `@0x20` + IP5306 battery
  PMIC `@0x75`, **shared bus**. Pull-ups **R30/R31 = 1k** (schematic). The old
  board's flaky reads / 1 s bus stalls were a *missing/faulty pull-up hardware
  fault*, bodged around (SDA/SCL→41/42, PCF INT→GPIO2). This revision has
  correct R30/R31 → run the stock bus, **no bodges**.
- **SPI/FSPI — GPIO 12 (SCLK) / 11 (MOSI) / 13 (MISO).** SD + CC1101 + NRF24×N
  + PN532, **shared by design**. Needs software bus arbitration
  (`Board.cpp` `spiBusy()` / `radio::currentOwner()`). The SD mount/dismount
  "issue" is this — *not* an I²C problem and *never a fault*. **Keep the
  arbitration permanently.**
- **GPIO2** is triple-purpose on stock HW: buzzer **and** VBAT/2 battery sense
  (R11/R16 100k divider → **ADC1_CH1**). It is *not* GPIO34 — `Pins.h`
  `BATTERY_ADC = 34` is a legacy/wrong value; the real sense pin is GPIO2.
  **Never bodge anything onto GPIO2.**
- **Battery:** the IP5306 I²C gauge is coarse (25% buckets) and is the source of
  the bogus hardcoded "70%". Prefer reading VBAT via the **GPIO2 ADC**
  (`analogReadMilliVolts(2) * 2`), calibrated.
- **USB / no CP2102 on this revision.** The CP2102 USB-C bridge was dropped
  (empty pads remain). The board flashes over the ESP32-S3 **built-in
  USB-Serial/JTAG** (`303A:1001`) on `/dev/ttyACM0` — esptool auto-resets into
  download mode, no manual BOOT/RESET. While our firmware runs it takes over the
  single USB PHY as a **TinyUSB OTG composite (MSC/HID, no CDC)**, so ttyACM0
  disappears and there is **no serial console**. The S3 has one USB PHY shared
  between USB-OTG and USB-Serial/JTAG, so we can't run the JTAG console
  alongside MSC/HID. **TODO:** add a CDC interface to the composite (MSC+CDC+HID)
  and route logging to it → serial back over the same cable, no soldering.

## Reference

- Schematics: `../ESP32-DIV/Schematic/{main,shield}-Shematic.jpg`.
- Plan / longer history: `../PLAN.md`.
- Vendor-firmware reverse-engineering & provenance: `../wtf/`.

## Current work — branch `stock-pins-battery`

Pivot off the old board's bodges onto stock hardware (free replacement board,
correct R30/R31). **Do not** wholesale-revert; selectively swap backends.

- **Stage 1 (done, flashed):** I²C → stock 8/9, GPIO2 INT path removed.
  *Gate:* on the I²C Health screen, `ok` climbing, `fail`≈0, `recoveries` 0, and
  responsive D-pad — confirm before proceeding.
- **Stage 2 (pending gate):** 400 kHz, strip the recovery/verify-on-change
  workarounds.
- **Stage 3 (pending):** `BatteryMonitor` on GPIO2 ADC (calibrated, LiPo curve)
  + a `Buzzer` with a 5 s safety auto-off.
- **Preserve:** input debounce, WiFi station/`WifiSetup`, keyboard case fix,
  SD/SPI arbitration, all radio/ducky/IR features.

## Conventions

- Every GPIO lives in `hw/Pins.h`.
- Heavy "why" comments are the norm and are load-bearing documentation — keep
  them honest (e.g. correct stale notes rather than leaving them).
