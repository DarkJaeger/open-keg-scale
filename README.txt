<a href="https://www.buymeacoffee.com/LocutusOFB"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="41" width="174"></a>

🍺 Open Keg Scale

DIY WiFi-enabled keg scale hardware designed to work with
open-plaato-keg

This repo includes two complete scale designs:

🍻 Standard Keg Scale (serving / monitoring)
🔄 Transfer Keg Scale (closed-loop transfers)
🚀 Overview

Open Keg Scale provides real-time weight data from your kegs and feeds it into open-plaato-keg for:

Remaining volume tracking
Pour detection
Transfer monitoring
Usage analytics

All fully local — no cloud required.

⚖️ Scale Types
🍻 Standard Keg Scale (Serving)
6

Designed for kegerators and serving setups.

Use Cases:

Track remaining beer in keg
Monitor pours in real time
Integrate with dashboards / Home Assistant

Features:

Stable 4-load-cell platform
Continuous weight monitoring
Pour detection via weight delta
🔄 Transfer Keg Scale (Closed Transfer)
6

Designed for closed-loop transfers from fermenter → keg.

Use Cases:

Monitor fill progress during transfer
Prevent overfilling
Track exact transferred volume

Features:

Compact / targeted design
High accuracy for fill tracking
Real-time feedback during transfer
🧰 What’s Included
✅ STL files for both scale designs
✅ ESP32-based firmware examples
✅ Wiring guidance
✅ Integration with open-plaato-keg
🏗️ Hardware Requirements
ESP32 (recommended)
HX711 load cell amplifier
Load cells (typically 4x 50kg)
5V power supply
Optional OLED display
3D printed parts (included in /stl)
🖨️ 3D Printed Parts

All required STL files are included:

Standard scale platform
Transfer scale platform
Mounting hardware / enclosures

Print in PLA or PETG depending on environment

🔌 Wiring Overview
Load Cells → HX711
HX711 → ESP32 (DT + SCK)
Optional OLED → I2C
⚙️ Firmware

Recommended: ESPHome

sensor:
  - platform: hx711
    name: "Keg Weight"
    dout_pin: GPIO21
    clk_pin: GPIO22
    gain: 128
    update_interval: 1s
🔗 Integration

This hardware feeds data into:

👉 open-plaato-keg

Supports:

REST API
MQTT
Home Assistant bridge
📡 Data Flow
[Load Cells] → [HX711] → [ESP32] → [WiFi] → [open-plaato-keg]
⚖️ Calibration
Tare empty scale
Add known weight
Adjust calibration factor
Save configuration
🧪 Roadmap
 Auto-discovery in open-plaato-keg
 OTA updates
 Battery-powered version
 Multi-scale controller
🤝 Related Project
open-plaato-keg – backend + dashboard
💡 Pro Tips
Use 4 load cells for stability
Keep HX711 wiring short
Calibrate with realistic loads
Solid mounting = better accuracy

Corny keg scale (Plaato) by FloppyKnockers on Thingiverse: https://www.thingiverse.com/thing:6007574
