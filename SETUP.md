# Setup Guide – Open Keg Scale & Transfer Keg Scale

Both projects use a **Wemos D1 Mini (ESP8266)** with an **HX711** ADC and **4 × 50 kg half-bridge load cells**.  
Choose the project that fits your needs:

| | Open Keg Scale | Transfer Keg Scale |
|---|---|---|
| **Display** | Web dashboard (open-plaato-keg server) | Local OLED 128×64 |
| **Protocol** | Blynk binary TCP | HTTP REST API |
| **Build tool** | Arduino IDE | PlatformIO |
| **Use case** | Permanent tap monitoring | Portable / transfer use |

---

## Table of Contents

1. [Hardware & Parts](#1-hardware--parts)
2. [Wiring](#2-wiring)
3. [Open Keg Scale – Software Setup](#3-open-keg-scale--software-setup)
4. [Open Keg Scale – First-Time Device Setup](#4-open-keg-scale--first-time-device-setup)
5. [Open Keg Scale – Calibration](#5-open-keg-scale--calibration)
6. [Open Keg Scale – OTA Updates](#6-open-keg-scale--ota-updates)
7. [Transfer Keg Scale – Software Setup](#7-transfer-keg-scale--software-setup)
8. [Transfer Keg Scale – First-Time Device Setup](#8-transfer-keg-scale--first-time-device-setup)
9. [Transfer Keg Scale – Calibration](#9-transfer-keg-scale--calibration)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Hardware & Parts

### Shared (both projects)

| Part | Notes |
|------|-------|
| Wemos D1 Mini | ESP8266, 4 MB flash — **2.4 GHz WiFi only** |
| HX711 breakout | Any common board; 24-bit ADC |
| 4 × 50 kg half-bridge load cell | RED / BLACK / WHITE 3-wire cells |
| Micro-USB cable | For initial flash |
| 5 V USB power supply | ≥ 500 mA |

### Transfer Keg Scale only

| Part | Notes |
|------|-------|
| OLED 128×64 | SSD1306 or SH1106, I2C, 4-pin (VCC / GND / SDA / SCL) |

### Optional

| Part | Notes |
|------|-------|
| Load cell combinator PCB | Simplifies the Wheatstone bridge wiring |
| 3D-printed enclosure | STL files in `open_keg_scale/3d printer files/` (Open Keg Scale), `transfer-keg-scale/transfer scale.3mf` (Transfer) |

---

## 2. Wiring

Both projects share the same **load cell → HX711** wiring.  
Refer to the `Wiring info.jpg` in each project folder for a visual diagram.

### 2a. Load Cells → HX711 (Wheatstone Bridge)

Wire the four half-bridge cells as one full bridge.  
Each cell has three wires: **RED** (excitation +), **BLACK** (excitation −), **WHITE** (signal).

```
Cell A (front-left)   RED  ──┐
Cell B (front-right)  RED  ──┤──→  HX711 E+
Cell C (back-right)   RED  ──┤
Cell D (back-left)    RED  ──┘

Cell A (front-left)   BLACK──┐
Cell B (front-right)  BLACK──┤──→  HX711 E−
Cell C (back-right)   BLACK──┤
Cell D (back-left)    BLACK──┘

Cell A (front-left)   WHITE──┐
Cell C (back-right)   WHITE──┘──→  HX711 A+   (diagonal pair)

Cell B (front-right)  WHITE──┐
Cell D (back-left)    WHITE──┘──→  HX711 A−   (opposite diagonal pair)
```

> **Why diagonals?** Opposite platform corners flex the same direction under load.
> Wiring them to the same signal leg gives additive output and cancels common-mode errors.

If your kit included a **combinator PCB**, wire each cell's three leads to it and connect the PCB's four output lines (E+, E−, A+, A−) directly to the HX711.

### 2b. HX711 → Wemos D1 Mini

#### Open Keg Scale

| HX711 | D1 Mini pin | GPIO |
|-------|-------------|------|
| VCC   | 3V3         | —    |
| GND   | GND         | —    |
| DT    | D6          | 12   |
| SCK   | D5          | 14   |

#### Transfer Keg Scale

| HX711 | D1 Mini pin | GPIO |
|-------|-------------|------|
| VCC   | 5V          | —    |
| GND   | GND         | —    |
| DOUT  | D5          | 14   |
| SCK   | D6          | 12   |

> The Transfer Keg Scale powers the HX711 from **5 V** for better noise immunity.

### 2c. OLED → D1 Mini (Transfer Keg Scale only)

| OLED | D1 Mini pin | GPIO |
|------|-------------|------|
| VCC  | 3V3         | —    |
| GND  | GND         | —    |
| SDA  | D2          | 4    |
| SCL  | D1          | 5    |

### 2d. Buttons & LED (Open Keg Scale)

| Signal | Pin |
|--------|-----|
| Tare / reset button | D3 (GPIO 0) — built-in FLASH button |
| Status LED | D4 (GPIO 2) — built-in LED, active LOW |

---

## 3. Open Keg Scale – Software Setup

### 3a. Install Arduino IDE

Download and install **Arduino IDE 2.x** from [arduino.cc](https://www.arduino.cc/en/software).

### 3b. Add ESP8266 Board Support

1. Open **File → Preferences**.
2. In *Additional boards manager URLs* add:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Open **Tools → Board → Boards Manager**, search **esp8266**, install the **esp8266 by ESP8266 Community** package.

### 3c. Select the Board

**Tools → Board → ESP8266 Boards → LOLIN(WEMOS) D1 mini**

Recommended upload settings:
- Upload Speed: `921600`
- CPU Frequency: `80 MHz`
- Flash Size: `4MB (FS:2MB OTA:~1019KB)`

### 3d. Install Libraries

**Tools → Manage Libraries**, search and install:

| Library | Author | Minimum version |
|---------|--------|-----------------|
| WiFiManager | tzapu | 2.0.17 |
| HX711 Arduino Library | Bogdan Necula | 0.7.5 |

`ArduinoOTA` ships with the ESP8266 core — no separate install needed.

### 3e. Flash the Firmware

1. Connect the D1 Mini via USB.
2. Open `open_keg_scale/keg_scale.ino` in Arduino IDE.
3. Select the correct **Port** under **Tools → Port**.
4. Click **Upload** (Ctrl+U).

---

## 4. Open Keg Scale – First-Time Device Setup

After the first flash the device has no WiFi credentials and boots into **setup mode** automatically.

1. On your phone or laptop, scan for WiFi networks. Connect to **`KegScale-Setup`** (no password).
2. A captive portal will open automatically, or browse to `http://192.168.4.1`.
3. Fill in all fields and click **Save**:

| Field | Description |
|-------|-------------|
| WiFi SSID | Your 2.4 GHz network name |
| WiFi Password | Your network password |
| Plaato Server Host | IP or hostname of the machine running [open-plaato-keg](https://github.com/DarkJaeger/open-plaato-keg) (e.g. `192.168.1.50`) |
| Server Port | Default `1234` |
| Auth Token | 32-character hex token from the open-plaato-keg web UI |
| Calibration Factor | Leave blank on first setup; calibrate after mounting the scale |
| Max Keg Volume (L) | `19` for a standard Cornelius / ball-lock keg |
| Beer Name | Displayed in the dashboard |

4. The device reboots, connects to your WiFi, then connects to the Plaato server.  
   The LED will slow-blink (500 ms) once fully connected and reporting.

> **Re-enter setup mode at any time:**  
> Hold the FLASH button for **4+ seconds** to wipe credentials, or use the web UI at  
> `http://<device-ip>/` → **Reset WiFi / Enter Setup Mode**.

### LED Status Reference

| Pattern | Meaning |
|---------|---------|
| Fast blink (100 ms) | Connecting to WiFi or server |
| Medium blink (250 ms) | TCP connected, waiting for auth |
| Slow blink (500 ms) | Fully connected and reporting |
| 3 short flashes | Tare complete |
| 4 short flashes | Calibration saved |

---

## 5. Open Keg Scale – Calibration

The scale ships with a calibration factor of `1.0` (uncalibrated). Calibrate once after physically mounting the load cells.

### Via the server UI (recommended)

1. Remove everything from the scale platform.
2. In the open-plaato-keg web interface, click **Tare**.
3. Place a known-weight object (e.g. a full keg, a bag of flour, a filled water container).
4. Enter its mass in **grams** in the *Calibrate with known weight* field and click **Send**.
5. The device saves the calibration factor to flash.

### Via Serial Monitor

1. Open **Tools → Serial Monitor** at `115200` baud.
2. Clear the platform and short-press the FLASH button to tare.
3. Place a known weight; note the raw ADC value printed.
4. Compute: `calibration_factor = raw_value / known_grams`
5. Enter the factor via the server UI calibrate command or web UI.

---

## 6. Open Keg Scale – OTA Updates

After the initial USB flash and WiFi join, the device advertises an Arduino OTA target.

### Via Arduino IDE

1. Ensure your computer and the device are on the same network.
2. Wait for the device to connect (slow LED blink).
3. **Tools → Port** → choose the **network port** for `keg-scale-<chipid>`.
4. Click **Upload**. When prompted for a password enter the **32-character auth token**.

### Via Web UI (easiest)

Browse to `http://<device-ip>/update`, log in with:
- **Username:** `admin`
- **Password:** your 32-character auth token

Upload the compiled `.bin` file directly.

---

## 7. Transfer Keg Scale – Software Setup

### 7a. Install PlatformIO

Install [Visual Studio Code](https://code.visualstudio.com/) then add the **PlatformIO IDE** extension from the Extensions Marketplace.

Alternatively install the PlatformIO CLI:
```bash
pip install platformio
```

### 7b. Open the Project

Open the folder `transfer-keg-scale/transfer-keg-firmware/` in VS Code.  
PlatformIO will automatically resolve all library dependencies listed in `platformio.ini` on the first build.

### 7c. Configure `src/main.cpp`

Edit the config block near the top of `src/main.cpp`:

```cpp
#define WIFI_SSID           "YourSSID"
#define WIFI_PASSWORD       "YourPassword"
#define SERVER_BASE_URL     "http://192.168.1.100:3000"   // open-plaato-server address
#define SCALE_ID            "scale1"                       // unique ID for this scale
#define CALIBRATION_FACTOR  -430.0f   // replace after running calibration sketch
#define DEFAULT_EMPTY_KG    3.8f      // weight of your empty keg in kg
#define DEFAULT_TARGET_KG   12.5f     // weight of a full keg in kg
```

> Set `DEFAULT_EMPTY_KG` and `DEFAULT_TARGET_KG` for your keg type.  
> A standard 19 L Cornelius keg weighs ~4 kg empty and ~23 kg full.

### 7d. Flash the Firmware

#### Option A – Pre-compiled binary (quickest)

Use `esptool.py` or the [ESP8266 Flash Download Tool](https://www.espressif.com/en/support/download/other-tools) to flash `transfer-keg-scale/firmware.bin` at address `0x0`.

```bash
esptool.py --port COM3 write_flash 0x0 transfer-keg-scale/firmware.bin
```

#### Option B – Build from source (PlatformIO)

```bash
cd transfer-keg-scale/transfer-keg-firmware
pio run -t upload
```

---

## 8. Transfer Keg Scale – First-Time Device Setup

1. Power the device. The OLED shows **"TRANSFER SCALE"** with no WiFi indicator.
2. Connect your phone or laptop to the WiFi AP **`TransferScale-Setup`** (password: `scaleme`).
3. Browse to `http://192.168.4.1`.
4. Fill in the setup portal:

| Field | Description |
|-------|-------------|
| WiFi SSID | Your 2.4 GHz network name |
| WiFi Password | Your network password |
| Server Base URL | Full URL to open-plaato-server (e.g. `http://192.168.1.100:3000`) |
| Scale ID | Unique identifier (e.g. `transfer1`) |
| Calibration Factor | From calibration sketch (Step 9); leave default for now |
| Empty Keg Weight (kg) | Weight of your empty keg |
| Target Weight (kg) | Weight of a full keg |

5. Click **Save**. The device reboots, connects to WiFi, and the OLED shows **[W]** when connected.

> **Re-enter setup mode:** Hold the FLASH button (D3) for **3+ seconds**.

### OLED Display Layout

```
┌────────────────────────┐
│ TRANSFER SCALE  [W]    │  ← [W] = WiFi connected
│                        │
│   8.42 kg              │  ← current weight (large text)
│   ████████░░  67%      │  ← fill progress bar
│                        │
│ Tgt:12.5kg  Emp: 3.8   │  ← target / empty weights
└────────────────────────┘
```

Progress % = `(currentWeight − emptyWeight) / (targetWeight − emptyWeight) × 100`

---

## 9. Transfer Keg Scale – Calibration

### Via Calibration Sketch

1. In `transfer-keg-scale/transfer-keg-firmware/src/`, rename:
   ```
   calibration.cpp.txt  →  calibration.cpp
   ```
2. In `platformio.ini`, comment out the `main.cpp` build source and uncomment `calibration.cpp` (or temporarily exclude `main.cpp` from compilation).
3. Flash and open Serial Monitor at `115200` baud:
   ```bash
   pio device monitor
   ```
4. Follow the on-screen prompts:
   - Remove everything from the platform when asked.
   - Place a **known weight** (e.g. a 10 kg object) when prompted.
   - The sketch prints `CALIBRATION_FACTOR = -430.5` (example).
5. Copy that value back into the `#define CALIBRATION_FACTOR` line in `main.cpp`.
6. Undo the `platformio.ini` changes, rename `calibration.cpp` back to `calibration.cpp.txt`, and re-flash `main.cpp`.

### Via Serial Commands (quick adjustment)

With `main.cpp` running, open Serial Monitor at `115200` baud:

| Key | Action |
|-----|--------|
| `t` | Tare (zero) the scale |
| `+` | Increase calibration factor by 10 |
| `-` | Decrease calibration factor by 10 |
| `r` | Print raw HX711 reading |
| `c` | Print current calibration factor |
| `?` | Show help |

Use `+` / `−` to nudge the factor until a known weight reads correctly, then save the final value to `main.cpp` and re-flash.

### Calibration Notes

- If readings are **negative**, negate the calibration factor.
- `scale.tare()` runs at boot — keep the platform **empty at power-on**.
- Re-tare after ~5 minutes to account for load cell thermal warm-up.

---

## 10. Troubleshooting

### Open Keg Scale

| Symptom | Fix |
|---------|-----|
| No `KegScale-Setup` AP appears | Hold FLASH button 4 s to wipe credentials and restart setup |
| LED stuck fast-blinking | Device cannot reach WiFi or Plaato server; check credentials and server IP |
| Weight always 0 or garbage | Check HX711 DT→D6, SCK→D5 wiring; verify 3V3 supply |
| OTA port not visible in IDE | Ensure device and PC are on the same subnet; firewall may block mDNS |
| Volume reads wrong after calibration | Re-tare with empty platform, then re-calibrate with known weight |

### Transfer Keg Scale

| Symptom | Fix |
|---------|-----|
| OLED stays blank | Check I2C address — try `0x3C` then `0x3D`; verify SDA→D2, SCL→D1 |
| `HX711 ERROR` on boot | Check DOUT→D5, SCK→D6; confirm 5 V supply to HX711 |
| Weight reads ~0 always | Calibration factor is wrong — re-run calibration sketch |
| Weight oscillates wildly | Load cell mounting issue; ensure cell corners move freely and aren't over-constrained |
| POST to server fails | Check `SERVER_BASE_URL`; device and server must be on the same network |
| WiFi never connects | Confirm 2.4 GHz SSID; D1 Mini does not support 5 GHz |
| No `[W]` on OLED after setup | Wrong SSID/password; hold FLASH 3 s to re-enter setup portal |
