# Open Keg Scale – Firmware

Arduino sketch for **Wemos D1 Mini (ESP8266)** + **HX711** + **4 × 50 kg half-bridge load cells**.  
Reports keg weight / volume to [open-plaato-keg](https://github.com/DarkJaeger/open-plaato-keg) using the Blynk binary TCP protocol.

---

## Parts

| Part | Notes |
|------|-------|
| Wemos D1 Mini | ESP8266, 4 MB flash |
| HX711 breakout | Any common board |
| 4 × 50 kg half-bridge load cell | The ones that came with the scale |

---

## Wiring

### Load Cells → HX711

Wire the four half-bridge cells as a single **full Wheatstone bridge** for best accuracy.  
Each cell has three wires: **RED** (E+), **BLACK** (E−), **WHITE** (signal).

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
Cell C (back-right)   WHITE──┘──→  HX711 A+    (diagonal pair)

Cell B (front-right)  WHITE──┐
Cell D (back-left)    WHITE──┘──→  HX711 A−    (other diagonal pair)
```

> **Why diagonals?** Opposite corners of a platform scale flex in the same
> direction when loaded, so wiring them to the same signal leg produces
> additive output and cancels common-mode errors.

### HX711 → Wemos D1 Mini

| HX711 | Wemos D1 Mini |
|-------|---------------|
| VCC   | 3V3           |
| GND   | GND           |
| DT    | D6 (GPIO 12)  |
| SCK   | D5 (GPIO 14)  |

### Button / LED

| Signal | Pin |
|--------|-----|
| Tare / reset button | D3 (GPIO 0) – built-in FLASH button |
| Status LED          | D4 (GPIO 2) – built-in LED (active LOW) |

---

## LED Status

| Pattern | Meaning |
|---------|---------|
| Fast blink (100 ms) | Connecting to WiFi or Blynk server |
| Medium blink (250 ms) | TCP connected, waiting for auth |
| Slow blink (500 ms) | Fully connected and reporting |
| 3 short flashes | Tare complete |
| 4 short flashes | Calibration saved |

---

## Required Libraries

Install both via **Arduino IDE → Tools → Manage Libraries**:

| Library | Author | Version |
|---------|--------|---------|
| **WiFiManager** | tzapu | ≥ 2.0.17 |
| **HX711 Arduino Library** | Bogdan Necula | ≥ 0.7.5 |

Board: **LOLIN(WEMOS) D1 mini** (under *ESP8266 Boards* – install ESP8266 core via Board Manager if needed).

---

## First-Time Setup

1. Flash `keg_scale.ino` to the Wemos D1 Mini.
2. On first boot the device creates a WiFi access point named **`KegScale-Setup`**.
3. Connect your phone/laptop to that AP (no password) and browse to `http://192.168.4.1`.
4. Fill in the form:
   - **WiFi SSID & password** – your home network
   - **Plaato Server Host** – IP or hostname of the machine running open-plaato-keg  
     (e.g. `192.168.1.50`)
   - **Server Port** – default `1234`
   - **Auth Token** – the 32-character hex token from the open-plaato-keg web UI
   - **Max Keg Volume (L)** – e.g. `19` for a standard Cornelius keg
   - **Beer Name** – displayed in the UI
5. Click **Save**. The device reboots and connects automatically.

> To reconfigure, hold the FLASH button for **4+ seconds** – this wipes the
> saved WiFi credentials and restarts the setup portal.

---

## Calibration

The scale ships with a calibration factor of `1.0` (uncalibrated). You must calibrate once before volume readings are accurate.

### Via the server UI (recommended)

1. Remove everything from the scale platform.
2. In the open-plaato-keg web interface, click **Tare**.
3. Place an object of known mass (e.g. a full water bottle, a bag of flour).
4. Enter the mass in **grams** in the *Calibrate with known weight* field and click **Send**.
5. The device saves the new calibration factor to flash.

### Via Serial

1. Open Serial Monitor at 115200 baud.
2. Remove everything from the scale and short-press the FLASH button to tare.
3. Place a known weight; note the raw reading printed on Serial.
4. Use the server UI calibrate command as above, or compute:  
   `cal_factor = raw_value / known_grams`

---

## Virtual Pin Reference

| Pin | Name | Direction | Description |
|-----|------|-----------|-------------|
| v48 | percent_of_beer_left | device → server | % of keg remaining |
| v49 | is_pouring | device → server | `"1"` while pouring |
| v51 | amount_left | device → server | Litres remaining |
| v53 | weight_raw | device → server | Total weight in grams |
| v54 | volume_raw | device → server | Liquid volume in L |
| v55 | pour_volume_raw | device → server | Last pour volume (L) |
| v59 | last_pour | device → server | Last pour volume (L) |
| v60 | tare | server → device | Send `"1"` to tare |
| v61 | known_weight_calibrate | server → device | Send grams to calibrate |
| v62 | empty_keg_weight | server → device | `"1"` = capture current; or send grams |
| v64 | beer_name | both | Beer name string |
| v71 | unit | server → device | `"1"`=metric `"2"`=US |
| v75 | measure_unit | server → device | `"1"`=weight `"2"`=volume |
| v76 | max_keg_volume | server → device | Max volume in litres |
| v81 | wifi_signal_strength | device → server | RSSI in dBm |
| v93 | firmware_version | device → server | e.g. `"1.0.0"` |

---

## Protocol Overview

The device speaks the **Blynk classic binary TCP protocol** (not Blynk IoT/v2).

```
Packet:   [cmd:u8][msg_id:u16be][length:u16be][body:bytes]
Response: [0x00  ][msg_id:u16be][status:u16be]            (no body)
```

1. Device connects TCP to `server_host:server_port`.
2. Sends **LOGIN** (cmd=2) with the auth token as body.
3. Server replies with a RESPONSE (status=200) using the same `msg_id`.
4. Device pushes data via **HARDWARE** (cmd=20) commands: body = `"vw\0<pin>\0<value>"`.
5. Device sends **PING** (cmd=6) every 30 s to keep the connection alive.
6. Server can push commands back to the device using HARDWARE write packets.
