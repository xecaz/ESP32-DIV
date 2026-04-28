# ESP32-DIV v2 Firmware — Plan

## Context

The hardware is good. The firmware is what makes the device feel broken.

An ESP32-S3 board (confirmed rev v0.2, **16 MB** flash via `esptool flash_id` on `/dev/ttyUSB0`) carries an ILI9341 touchscreen, 5 keys on a PCF8574 I²C expander, WS2812s, SD card, CC1101 sub-GHz, 3× NRF24 (2.4 GHz), IR TX/RX, BLE, and a USB-OTG port in addition to the CP2102 UART. The stock firmware from cifertech (`ESP32-DIV/ESP32-DIV/`, ~20 kloc across 10 .cpp/.h files) has three pathologies that all trace to the same architectural choice:

1. **UI unresponsive, keys miss / double-fire.** Every feature is a `while (!feature_exit_requested) { ... }` that takes over `loop()`. Inputs are read by `isButtonPressed()` (`ESP32-DIV.ino:222–224`), a bare PCF8574 read with no debounce, no edge detection, and a hard `delay(200)` after every handled press (`ESP32-DIV.ino:2418–2461`). When a feature is running, its own inner loop starves I²C input polling.
2. **On-screen keyboard is uppercase-only.** `wifi.cpp` defines `keyboardLayout[]` *twice* — lowercase at 2197, uppercase at 3948 — same symbol, so the linker picks the last one. `KeyboardUI.cpp` has no shift/layer switching, and the space key is silently dropped (`KeyboardUI.cpp:169`). This makes wifi password entry essentially useless, which breaks the device's own golden path.
3. **No path to USB-side usefulness.** Adding USB composite (MSC + CDC + HID + HCI) requires the USB stack to keep being serviced while a radio feature runs. The current "one feature owns the CPU" model precludes that.

The goal is a new firmware that:
- Fixes the UI, input, and keyboard (root-cause fixes, not band-aids).
- Preserves every existing radio feature (WiFi, BLE, NRF24, CC1101, IR — the off-the-shelf library glue is fine; it's the scaffolding around it that's broken).
- Presents the device to a host computer over USB as a **composite device**: MSC (SD card), CDC (debug + radio command protocol), HID (wired Ducky), and — as a mode-switch — BLE HCI-over-USB.
- Offers a **HackRF-style Appliance mode**: a reboot-gated mode where the UI task is not even started, all CPU goes to USB + radios, every radio is exposed host-side simultaneously, and only a long-press of SELECT reboots back to Standalone.
- Ships on **PlatformIO + ESP-IDF with Arduino-as-component**, so we can use both Arduino radio libraries (TFT_eSPI, RF24, ELECHOUSE_CC1101, IRremote, NimBLE) *and* IDF-only features (TinyUSB composite, controller-only BLE HCI, ESP-HOSTED-style patterns if needed later).

## Approach

Rewrite the scaffold (input → UI state machine → radio manager → USB composite). Port the feature-specific radio code from the existing `.cpp` files into feature **screens** and **drivers** that plug into the scaffold. The existing firmware becomes reference documentation for pin mapping and library usage.

### Architecture

Three cooperating subsystems, each a FreeRTOS task:

**Input task** (core 0, 5 ms tick)
- Polls PCF8574 over I²C, polls XPT2046 touch.
- Per-key state machine: `IDLE → DEBOUNCE_DOWN → DOWN → REPEAT → DEBOUNCE_UP`, configurable debounce (40 ms), long-press (800 ms), repeat rate (120 ms) — constants already defined at `shared.h:198–206` but currently unused.
- Publishes `InputEvent { KEY_DOWN | KEY_UP | KEY_REPEAT | TOUCH_DOWN | TOUCH_MOVE | TOUCH_UP, data }` into a FreeRTOS queue.

**UI task** (core 0)
- Screen stack: MainMenu → Submenu → Feature → Keyboard (modal).
- Each screen implements `onEnter / onEvent / onTick / onRender / onExit`. No blocking loops anywhere in UI code. Rendering runs on a tick (30–60 FPS cap).
- Consumes the input queue. Both touch and hardware keys dispatch through the same screen API, so the keyboard (and every feature) is operable by either.

**Radio worker task** (core 1)
- Owns the shared SPI bus (SD / CC1101 / NRF24 all share pins 11/12/13). A `RadioManager` singleton grants exclusive ownership of a bus + CS pin for the duration of a driver call, using a FreeRTOS mutex.
- Each feature registers a `RadioJob` (start / tick / stop). Starting a feature posts a job; UI keeps running, keyboard still works, status bar updates.
- Handles pin-conflict exclusivity: **IR pins (14, 21) overlap with NRF24 #3 (CE=14, CSN=21)**. A runtime "shield profile" setting (stored in NVS) says which is populated on this physical board; the manager refuses to bring the other up.

**USB task** (owned by TinyUSB, runs on its own)
- Composite device descriptor: MSC + CDC #0 (console) + CDC #1 (radio protocol) + HID (keyboard + consumer).
- Remains serviced at all times because the CPU is no longer monopolized by a feature loop.

### Fixing the three root causes

| Issue | Fix |
|---|---|
| UI unresponsive | Event-driven UI task; features don't own the loop. |
| Missed / double key | Proper debounce state machine in InputTask; hardware keys dispatched as events. |
| Uppercase-only keyboard | New `KeyboardScreen` with four layouts (lower / upper / numbers / symbols), shift key, layer key, working space bar. Delete both `keyboardLayout[]` duplicates in `wifi.cpp` (2197, 3948) — layouts live inside the Keyboard screen. |

### USB composite details

- **MSC**: expose SD card as USB drive. Hot-plug handling — when host claims SD, local features that write (PCAP capture, profile save) pause with a user-visible "SD busy" state. Settings toggle to force-release.
- **CDC #0**: log + debug console. `HWCDC`-compatible so `pio device monitor` just works.
- **CDC #1 (radio protocol)**: line-based command protocol. Subset compatible with rfcat's text protocol for CC1101 so existing Python tools (`rflib`, `rfcat`) drive it. Native-but-simple verbs for NRF24 and IR. Commands like:
  - `radio cc1101 ; set freq 433920000 ; set mod ook ; tx hex DEADBEEF ; rx raw 2000`
  - `radio nrf24 0 ; set chan 76 ; rx promisc`
  - `radio ir ; tx nec 0x20DF10EF`
- **HID**: wired Ducky. Reuse the existing script parser from `ducky.cpp` but feed a `USBHIDKeyboard` sink instead of (or in addition to) BLE HID.
### USB modes (selectable in Settings, reboot-gated)

`Settings → USB Mode` offers three modes. Changing mode writes NVS and reboots.

- **Standalone** (default). Local UI owns the device. USB presents MSC + CDC console + HID Ducky. Radio features are driven from the TFT.
- **Bridge**. Local UI still runs. In addition to standalone USB interfaces, the CDC radio-protocol interface is live so a host can drive CC1101 / NRF24 / IR over USB while the user can also use the menu. Useful for scripted experiments with visual feedback.
- **Appliance** (HackRF-style). Device is a pure USB radio peripheral.
  - **No UI task.** TFT shows a static status card (mode name, MACs, USB state, radio activity LEDs) redrawn on radio events only — no touch handling, no menu.
  - **No InputTask menu dispatch.** A single hardware key (`SELECT` held 3 s) triggers reboot back to Standalone; otherwise input is ignored. This avoids "I can't get out."
  - **All CPU cycles to USB + radios.** UI core (core 0) is freed up for TinyUSB task + USB DMA handling. Radio workers own core 1.
  - **Every radio is host-driven**: MSC + CDC console + CDC radio-protocol + HID + **BLE HCI-over-USB** all active at once. The BT controller runs in controller-only mode (`CONFIG_BT_CTRL_HCI_MODE_VHCI`), bridged to a dedicated CDC interface (host runs `btattach -B /dev/ttyACM2 -P h4`, gets `hci0`).
  - **Reboot required to exit** — this is by design, matching HackRF's dedicated-appliance feel and preventing mode-switch bugs mid-operation.
  - Linux is the primary target (BlueZ + rfcat + LIRC ecosystem); Windows MSC + CDC + HID work, BLE HCI does not.

### Build system & partition layout

`platformio.ini` with `framework = arduino, espidf` (arduino-as-component). Custom `partitions_16mb.csv`:

```
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x400000,    # 4 MB app A
app1,     app,  ota_1,            0x400000,    # 4 MB app B (OTA)
littlefs, data, spiffs,           0x700000,    # 7 MB LittleFS (profiles, settings, assets)
coredump, data, coredump,         0x10000,
```

Leaves SD for captures/logs/scripts as today. 4 MB app slots easily fit the rewrite (current monolith is 1.37 MB).

### Critical files (new)

```
platformio.ini
sdkconfig.defaults                 # TinyUSB enabled, BLE controller mode build-flag, LittleFS
partitions_16mb.csv
src/main.cpp                       # replaces ESP32-DIV.ino
src/hw/Pins.h                      # single source of truth for GPIOs (replaces shared.h pin block)
src/hw/Board.{h,cpp}               # TFT init, SD mount, NeoPixel, battery, buzzer
src/input/InputTask.{h,cpp}        # PCF8574 + XPT2046 polling, debounce, event queue
src/ui/Screen.h                    # Screen interface
src/ui/UiTask.{h,cpp}              # screen stack + render tick
src/ui/screens/MainMenu.{h,cpp}
src/ui/screens/SubMenu.{h,cpp}
src/ui/screens/Keyboard.{h,cpp}    # four layouts, shift, layer, space, arrow-key nav
src/ui/screens/Settings.{h,cpp}    # USB mode, shield profile, theme, touch cal
src/ui/screens/feature/*.{h,cpp}   # one screen per radio feature
src/radio/RadioManager.{h,cpp}     # SPI bus mutex, pin-conflict arbitration
src/radio/WifiDriver.{h,cpp}       # ports logic from wifi.cpp (packet mon, scanner, deauth, beacon, portal)
src/radio/BleDriver.{h,cpp}        # ports bluetooth.cpp (scanner, sniffer, jammer via NRF24, Sour Apple)
src/radio/Nrf24Driver.{h,cpp}      # 3× RF24 instances, spectrum, protokill
src/radio/Cc1101Driver.{h,cpp}     # ports subghz.cpp (replay, jammer, profiles)
src/radio/IrDriver.{h,cpp}         # ports ir.cpp (record, replay, profiles)
src/usb/UsbComposite.{h,cpp}       # TinyUSB descriptors + glue
src/usb/MscSdBridge.{h,cpp}        # MSC callbacks → SD
src/usb/CdcConsole.{h,cpp}
src/usb/CdcRadioCli.{h,cpp}        # rfcat-subset + native verbs
src/usb/HidDucky.{h,cpp}           # wired Ducky using existing script parser
src/usb/HciBridge.{h,cpp}          # VHCI → CDC (mode-switch)
src/storage/Settings.{h,cpp}       # NVS-backed
src/storage/Profiles.{h,cpp}       # SubGHz / IR profile files on SD (format preserved)
```

### Work order (milestones)

1. **PlatformIO skeleton** — boot banner on TFT, SD mount, NVS settings, nothing else. Proves build/flash/run.
2. **Input pipeline** — InputTask + event queue; demo screen that logs every event. Validates debounce: rapid mash = exactly one event per press, held = proper repeat, no I²C thrash.
3. **UI state machine** — MainMenu + SubMenu + Settings screens, no features yet. Validates non-blocking render + navigation.
4. **Keyboard screen** — four layouts, shift, layer, space, backspace, enter, arrow-key navigation in parallel with touch. This is the device's credibility test.
5. **RadioManager + WiFiDriver scanner** — port the simplest feature as the architecture proof. Scan list populates live while keyboard and menu stay responsive.
6. **Port remaining features** — one per commit: WiFi packet monitor → beacon spammer → deauth → captive portal → BLE scanner/sniffer → BLE jammer (NRF24) → Sour Apple → NRF24 spectrum → Protokill → CC1101 replay → CC1101 jammer → IR record/replay → Ducky (BLE).
7. **USB composite** — in this order: MSC (biggest UX win) → CDC console → CDC radio protocol → HID Ducky (wired).
8. **Appliance mode** — NVS-selected boot path. On boot, if mode == Appliance, skip `UiTask::start()`, render the static status card once, start all radio drivers in host-driven state, start BLE controller in VHCI mode, route to HCI-CDC. Long-press SELECT → set NVS = Standalone, reboot.
9. **Bridge mode** — lighter variant of Appliance: UI still runs, but CDC radio protocol is active alongside local feature use.
10. **Polish** — persistent touch calibration in NVS, OTA via second app slot, LED/buzzer feedback, battery telemetry in status bar.

### Reuse from existing firmware

These existing files are the authoritative reference for protocol logic — port, don't reinvent:
- `wifi.cpp` — 802.11 raw-frame injection, PCAP capture framing, captive portal HTML
- `bluetooth.cpp` — NRF24 BLE-channel hop tables, Sour Apple payload, BLE sniffer parse
- `subghz.cpp` — CC1101 register values, RCSwitch protocol map, profile binary format
- `ir.cpp` — IRrecv/IRsend flow, profile binary format
- `ducky.cpp` — script grammar (STRING / DELAY / KEY / REPEAT / WAIT_FOR)
- `Touchscreen.cpp` — XPT2046 calibration math
- `SettingsStore.cpp` — JSON schema (re-home into NVS, keep field names)

### Pin map fixes

- Honor the existing pin conflict between IR (`14,21`) and NRF24 #3 (`14,21`) with an explicit runtime `ShieldProfile` setting (`NRF24_TRIPLE` vs `NRF24_DUAL_PLUS_IR`). `RadioManager::acquire()` refuses contradictory requests.
- Arbitrate shared SPI bus (SD / CC1101 / NRF24 on 11/12/13) through a single mutex in `RadioManager` — no more "mutually exclusive by main-loop convention."
- Document the buzzer/backlight/battery-divider GPIO overlaps that already exist on the v2 board so they're visible in code, not hidden in the schematic.

## Verification

Local (on-device) regressions — run each after its milestone:
- **Input**: open the input-log demo, mash 5 keys in 500 ms → 5 distinct `KEY_DOWN` events, no doubles, no misses. Hold a key 2 s → 1 down + repeats at ~120 ms after 500 ms lead. Run while a WiFi scan is active — same result.
- **Keyboard**: enter `P@ssw0rd! Test 123` — every character type (lower, upper, symbol, digit, space) lands. Navigate with arrow keys as well as touch.
- **Every radio feature**: exercise from the new UI exactly as the original firmware does (scan lists populate, beacon spam visible on a sniffer, IR replay triggers a TV, CC1101 replay opens a test remote target, etc.). Compare against RESTORE firmware behavior on the same bench.

USB-side (host = Linux):
- `lsblk` shows SD as `/dev/sdX`, mountable, files match on-device view when exclusive.
- `/dev/ttyACM0` = console (`pio device monitor`), `/dev/ttyACM1` = radio protocol.
- `rfcat` (or a short Python script) completes a 433.92 MHz OOK replay via `/dev/ttyACM1`; confirm with a second SDR.
- `dmesg | grep -i hid` shows keyboard; Ducky script typed into a text editor window.
- In `Appliance` mode: all of `sdX` (MSC), `ttyACM0` (console), `ttyACM1` (radio protocol), `ttyACM2` (HCI), and HID keyboard appear at once; `btattach -B /dev/ttyACM2 -P h4 -S 921600 && hciconfig hci0 up && hcitool lescan` returns surrounding BLE devices; rfcat + radio-protocol CDC work concurrently with HCI; TFT shows the static status card only; touch does nothing; holding SELECT ≥3 s reboots back to Standalone.

Bench tooling (no network calls, no external services): esptool for flash/monitor, a cheap RTL-SDR for 433 MHz confirmation, a second ESP32 or phone for BLE confirmation, any IR receiver for IR confirmation.

## Open items (decide during implementation, not blockers)

- Exact CDC radio-protocol grammar — start with rfcat-subset for CC1101, iterate on NRF24/IR verbs as real host-side scripts get written.
- Whether to expose `esp-hosted`-style WiFi passthrough over USB (host gets a network interface via the device's WiFi). Heavy; deferred until the four USB modes above are solid. Hooks left in `UsbComposite` so it can be added without restructuring.
- Whether to keep cifertech's Arduino-format `/config/settings.json` on SD as an import path for existing users. Cheap to add; decide when Settings screen lands.
