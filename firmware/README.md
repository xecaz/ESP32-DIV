# ESP32-DIV firmware

Independent, ground-up firmware for an ESP32-S3 "ESP32-DIV" wireless multitool
(WiFi / BLE / 2.4 GHz NRF24 / sub-GHz CC1101 / IR / USB-HID). PlatformIO,
Arduino-on-IDF, FreeRTOS task architecture with a touch + D-pad UI.

This is a clean reimplementation of the CiferTech ESP32-DIV concept — not a fork
of the vendor's contractor firmware. It adds reliable input debounce, WiFi
station setup, a real voltage-based battery gauge, and shared-SPI bus
arbitration that the stock firmware lacks.

The story behind it is written up at
<https://xecaz.com/blog.html#esp32-div-ctrlvoid>.

## Prebuilt firmware (no toolchain needed)

A ready-to-flash merged image is committed here as `esp32div-MK1-v1.0.0.bin`
(bootloader + partitions + OTA data + app, flash at `0x0`). See
[`how-to-flash.md`](how-to-flash.md) for Windows/macOS/Linux instructions (web
flasher or `esptool`). To regenerate it after a `pio run`:

```
B=.pio/build/esp32div
BA=~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
.venv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 \
  merge_bin --flash_mode keep --flash_freq keep --flash_size keep \
  -o esp32div-MK1-v1.0.0.bin \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xe000 $BA 0x10000 $B/firmware.bin
```

## Build & flash from source

```
cd firmware
.venv/bin/pio run -e esp32div            # build
.venv/bin/pio run -e esp32div -t upload  # flash over USB (/dev/ttyACM0)
.venv/bin/pio device monitor             # USB-CDC serial console
```

The CP2102 bridge was dropped this revision, so flashing goes over the ESP32-S3
built-in USB-Serial/JTAG. The running firmware claims native USB as a TinyUSB
composite (MSC + HID + **CDC console**), so `pio device monitor` shows logs over
the same USB-C cable — but to flash you must first drop the running firmware
into ROM download mode with the **manual BOOT+RESET combo** (hold BOOT, tap
RESET, release BOOT): the CDC can trigger a reboot but the OTG→ROM USB handoff
isn't seamless. Logs go to the global `USBSerial` (`Serial`/UART0 is dead). The
on-device **I²C Health** screen is also handy for live I²C bus stats.

## Hardware & architecture

See [`CLAUDE.md`](CLAUDE.md) for the board's bus map, pin assignments, and the
reasoning behind them, and [`../PLAN.md`](../PLAN.md) for roadmap/history.
GPIO assignments live in [`src/hw/Pins.h`](src/hw/Pins.h) (single source of
truth).

## Status

Active branch `stock-pins-battery`: moved off the old-board hardware bodges onto
the stock board (correct R30/R31 I²C pull-ups, no bodge wires). Done: stock I²C
bus at 400 kHz, GPIO2-ADC battery gauge (calibrated, LiPo curve, header icon),
and a USB-CDC serial console added to the composite. Still open: buzzer driver
and battery-curve linearity check. See the "Current work" section of `CLAUDE.md`.
