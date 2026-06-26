# Flashing CTRL//VOID to an ESP32-DIV

This folder ships a **prebuilt firmware image** so you can run CTRL//VOID without
installing a full build toolchain. You only need a flasher and a USB-C cable.

- **Board:** ESP32-S3 "ESP32-DIV" (ESP32-S3-WROOM-1U, 16 MB flash).
- **Image:** `esp32div-MK1-v1.0.0.bin` — a single merged image (bootloader +
  partition table + OTA data + app). Flash it at offset **`0x0`**.
- **Cable:** a real USB-C **data** cable (some are charge-only).

> **Heads-up — download mode.** While CTRL//VOID is running it takes over the
> USB port as a composite device, so flashers often can't auto-reset it. If a
> flash fails to connect, put the board in **download mode** by hand:
> **hold `BOOT`, tap `RESET`, release `BOOT`**, then flash. (Harmless to do
> even when it isn't strictly needed.)

---

## Option A — Web flasher (nothing to install) — easiest

1. Use a **Chromium-based browser** (Chrome or Edge) — they support Web Serial.
   Firefox/Safari do **not** work.
2. Open the official Espressif web flasher: <https://espressif.github.io/esptool-js/>
3. Set **Baudrate** to `921600`, click **Connect**, and pick the board's serial
   port (see "Find the port" below). If it won't connect, do the BOOT+RESET combo.
4. Add a flash entry: **Flash Address** `0x0`, **File** = `esp32div-MK1-v1.0.0.bin`.
5. Click **Program**. When it finishes, press `RESET` — the board boots CTRL//VOID.

---

## Option B — esptool (command line)

### 1. Install esptool (needs Python 3)

| OS | Install |
|---|---|
| **Windows** | Install Python from <https://python.org> (tick *"Add Python to PATH"*), then in a terminal: `pip install esptool`. The S3's native USB needs no driver on Win10/11. |
| **macOS** | `pip3 install esptool` (or `brew install esptool`). |
| **Linux** | `pip install esptool`. For port access: `sudo usermod -aG dialout $USER`, then log out/in. |

### 2. Find the serial port

| OS | Port looks like | How to find |
|---|---|---|
| **Windows** | `COM5` | Device Manager → *Ports (COM & LPT)* → "USB Serial Device" |
| **macOS** | `/dev/cu.usbmodem1101` | `ls /dev/cu.usbmodem*` |
| **Linux** | `/dev/ttyACM0` | `ls /dev/ttyACM*` |

### 3. Put the board in download mode

Hold **`BOOT`**, tap **`RESET`**, release **`BOOT`**.

### 4. Flash

```bash
esptool.py --chip esp32s3 --port <PORT> --baud 921600 write_flash 0x0 esp32div-MK1-v1.0.0.bin
```

Replace `<PORT>` with your port from step 2 (e.g. `COM5`,
`/dev/cu.usbmodem1101`, or `/dev/ttyACM0`). On some systems the command is
`esptool` instead of `esptool.py`, or `python -m esptool`.

When it prints `Hash of data verified.` and `Hard resetting`, the board reboots
into CTRL//VOID.

---

## After flashing

- The board boots to the CTRL//VOID splash, then the main menu.
- A **USB-CDC serial console** is available at **115200 baud** for logs
  (`pio device monitor`, or any serial terminal on the same port).

## Building from source instead

If you'd rather build it yourself, see [`README.md`](README.md) and
[`CLAUDE.md`](CLAUDE.md) — it's a PlatformIO project (`pio run -t upload`).

## Troubleshooting

- **"Failed to connect" / "No serial data received":** put the board in download
  mode (hold `BOOT`, tap `RESET`, release `BOOT`) and try again.
- **Permission denied on Linux:** add yourself to the `dialout` group (see install).
- **Nothing on the port list:** try a different USB-C **data** cable; confirm the
  board powers on (screen lights up).
