# Transfer Keg Firmware
**Platform:** Wemos D1 Mini (ESP8266)  
**Server:** open-plaato-server  
**Companion:** Android app via open-plaato API

---

## Wiring

### HX711 → D1 Mini
| HX711 | D1 Mini      | GPIO  |
|-------|--------------|-------|
| DOUT  | D5           | 14    |
| SCK   | D6           | 12    |
| VCC   | 5V           | —     |
| GND   | GND          | —     |

### OLED (SSD1306 128×64) → D1 Mini
| OLED | D1 Mini | GPIO |
|------|---------|------|
| SDA  | D2      | 4    |
| SCL  | D1      | 5    |
| VCC  | 3.3V    | —    |
| GND  | GND     | —    |

### Load Cells → HX711
Wire 4 cells as a Wheatstone bridge:
```
Cell A: E+ → E+,  S+ → A+
Cell B: E+ → E+,  S+ → B+   (opposite corner to A)
Cell C: E- → E-,  S- → A-
Cell D: E- → E-,  S- → B-   (opposite corner to C)
```
Most 4-cell platform kits include a combinator PCB — use that if available.

---

## Quick Start

### 1. Edit `src/main.cpp` config block
```cpp
#define WIFI_SSID       "YourSSID"
#define WIFI_PASSWORD   "YourPassword"
#define SERVER_BASE_URL "http://192.168.1.100:3000"
#define SCALE_ID        "scale1"
#define CALIBRATION_FACTOR  -430.0f   // find with calibration sketch
#define DEFAULT_EMPTY_KG    3.8f
#define DEFAULT_TARGET_KG   12.5f
```

### 2. Find your calibration factor
1. Rename `calibration.cpp.txt` → `calibration.cpp` (comment out main.cpp)
2. Flash, open Serial Monitor @ 115200
3. Follow prompts with a known weight (e.g. 10 kg)
4. Copy printed `CALIBRATION_FACTOR` back into `main.cpp`
5. Restore `main.cpp` as main entry point

### 3. Flash
```bash
pio run -t upload
pio device monitor
```

---

## API Contract

### POST `/api/transfer-scales/:id`
Sent every 5 seconds (configurable via `POST_INTERVAL_MS`).

**Request body:**
```json
{
  "weight": 8.42,
  "unit": "kg",
  "emptyWeight": 3.8,
  "targetWeight": 12.5
}
```

### GET `/api/transfer-scales/:id`
Polled every 30 seconds. Used to sync `emptyWeight` / `targetWeight` from server.

**Expected response (200 OK):**
```json
{
  "emptyWeight": 3.8,
  "targetWeight": 12.5
}
```
Any field absent from the response is silently ignored (device keeps its current value).

---

## OLED Layout
```
┌────────────────────────┐
│ TRANSFER SCALE  [W]    │  ← [W] = WiFi connected, [ ] = offline
│                        │
│   8.42 kg              │  ← large text (TextSize 2)
│   ████████░░  67%      │  ← progress bar (net weight / net target)
│                        │
│ Tgt:12.5kg  Emp: 3.8   │  ← target / empty keg weight
└────────────────────────┘
```

Progress % = `(currentWeight - emptyWeight) / (targetWeight - emptyWeight)`

---

## Serial Commands (115200 baud)
| Key | Action |
|-----|--------|
| `t` | Tare (zero) the scale |
| `+` | Increase calibration factor by 10 |
| `-` | Decrease calibration factor by 10 |
| `r` | Print raw HX711 reading |
| `c` | Print current calibration factor |
| `?` | Show help |

---

## Calibration Notes
- The bogde HX711 library `get_units()` divides by the calibration factor, so:
  - `calFactor = raw_reading / known_weight_kg`
  - If readings are inverted (negative), negate the factor
- `scale.tare()` is called at boot — ensure nothing is on the platform at power-on
- Temperature drift: re-tare after the load cell warms up (~5 min)

---

## Troubleshooting
| Symptom | Fix |
|---------|-----|
| OLED blank | Check I2C address (try `0x3C` or `0x3D`); run I2C scanner |
| HX711 ERROR on boot | Check DOUT/SCK wiring; ensure 5V supply to HX711 |
| Weight reads ~0 always | Calibration factor wrong; re-run calibration sketch |
| Weight oscillates wildly | Mechanical issue with load cell mounting; check cell corners are free |
| POST fails silently | Check SERVER_BASE_URL; ensure device and server on same network |
| WiFi never connects | Check credentials; D1 Mini is 2.4 GHz only |

---

## Dependencies (`platformio.ini`)
```
bogde/HX711 @ ^0.7.5
adafruit/Adafruit SSD1306 @ ^2.5.7
adafruit/Adafruit GFX Library @ ^1.11.9
adafruit/Adafruit BusIO @ ^1.14.5
```
All resolved automatically by PlatformIO.
