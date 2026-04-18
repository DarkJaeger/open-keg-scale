/*
 * Open Keg Scale Firmware v1.0.0
 * ================================
 * Hardware : Wemos D1 Mini (ESP8266) + HX711 + 4x 50 kg Half-Bridge Load Cells
 * Server   : open-plaato-keg  (https://github.com/DarkJaeger/open-plaato-keg)
 * Protocol : Blynk binary TCP (port 1234 by default)
 *
 * ── Load Cell Wiring (4x Half-Bridge → 1 Full Wheatstone Bridge) ─────────────
 *
 *   This build uses the common bathroom-scale 3-wire cells with:
 *     RED   = C  (common)
 *     WHITE = +  (signal leg)
 *     BLACK = −  (signal leg)
 *
 *   The four cells are combined into one full bridge before reaching the HX711.
 *   Follow the same grouping shown in your scale wiring diagram:
 *
 *     RED / C wiring:
 *       Cell A (upper-left)   RED  ┐
 *       Cell B (upper-right)  RED  ├──────────────→  HX711 E+
 *       Cell C (lower-left)   RED  ┘
 *       Cell D (lower-right)  RED  ───────────────→  HX711 E−
 *
 *     BLACK / − wiring:
 *       Cell A (upper-left)   BLACK ──────────────→  HX711 A−
 *       Cell B (upper-right)  BLACK ┐
 *       Cell C (lower-left)   BLACK ┤
 *       Cell D (lower-right)  BLACK ┘
 *
 *     WHITE / + wiring:
 *       Cell A (upper-left)   WHITE ──────────────→  HX711 A+
 *       Cell B (upper-right)  WHITE ┐
 *       Cell C (lower-left)   WHITE ┤
 *       Cell D (lower-right)  WHITE ┘
 *
 *   Note: load-cell wire colours vary by manufacturer. This mapping matches
 *   the wiring diagram for this specific scale, not the generic RED/E+/BLACK/E−/
 *   WHITE/signal convention used by some other half-bridge sets.
 *
 * ── HX711 ↔ Wemos D1 Mini ────────────────────────────────────────────────────
 *
 *   HX711 VCC  →  3.3 V
 *   HX711 GND  →  GND
 *   HX711 DT   →  D6  (GPIO 12)
 *   HX711 SCK  →  D5  (GPIO 14)
 *
 * ── Button / LED ─────────────────────────────────────────────────────────────
 *
 *   D3 (GPIO 0)  = built-in FLASH button (active LOW)
 *     Short press  (<2 s)  → Tare (zero) the scale
 *     Long press   (>4 s)  → Erase WiFi credentials & restart into setup portal
 *
 *   D4 (GPIO 2) = built-in LED (active LOW)
 *     Fast blink  100 ms   → Connecting to WiFi or Blynk server
 *     Medium blink 500 ms  → Connected, idle
 *     Solid ON             → Sending data / taring
 *     2 quick flashes      → OTA ready after WiFi connect
 *
 * ── OTA Updates ────────────────────────────────────────────────────────────────
 *
 *   After the first USB flash, the device supports Arduino OTA updates over
 *   the local network.
 *
 *   Hostname:  keg-scale-<chipid>
 *   Password:  same as the configured auth token
 *   Web UI:    http://<device-ip>/         (status page)
 *              http://<device-ip>/update   (firmware upload)
 *
 * ── First-Time Setup ──────────────────────────────────────────────────────────
 *
 *   1. Flash this sketch to a Wemos D1 Mini.
 *   2. On first boot the device creates a WiFi AP called "KegScale-Setup".
 *   3. Connect to that AP (no password) and open http://192.168.4.1
 *   4. Fill in:
 *        WiFi SSID & password
 *        Plaato server host  (IP or hostname running open-plaato-keg)
 *        Plaato server port  (default 1234)
 *        Auth token          (32-character hex string from the server UI)
 *        Calibration factor  (optional, if already known)
 *        Max keg volume      (litres, e.g. 19 for a Corny keg)
 *        Beer name
 *   5. Save. The device reboots and connects automatically.
 *
 * ── Calibration ───────────────────────────────────────────────────────────────
 *
 *   Method A – via server UI (recommended):
 *     a. Make sure the scale is empty, then click "Tare" in the UI.
 *     b. Place a weight of known mass on the scale.
 *     c. Enter that mass (grams) in the "Calibrate" field and click Send.
 *
 *   Method B – via button:
 *     a. Remove everything from the scale.
 *     b. Short-press the FLASH button to tare.
 *     c. Place a known weight and note the raw value printed on Serial (115200).
 *     d. Set calibration factor via server UI or recalculate manually.
 *
 * ── Required Arduino Libraries (install via Library Manager) ──────────────────
 *
 *   1. WiFiManager          by tzapu              ≥ 2.0.17
 *   2. HX711 Arduino Library by Bogdan Necula      ≥ 0.7.5
 *
 *   Board: "LOLIN(WEMOS) D1 mini" – ESP8266 @ 80 MHz, 4 MB Flash
 */

// ─── Includes ─────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>          // tzapu/WiFiManager
#include <HX711.h>                // bogde/HX711
#include <EEPROM.h>

// ─── Pin Definitions ──────────────────────────────────────────────────────────

#define HX711_DOUT_PIN   12   // D6
#define HX711_SCK_PIN    14   // D5
#define BUTTON_PIN        0   // D3 – built-in FLASH button (active LOW)
#define LED_PIN           2   // D4 – built-in LED (active LOW)

// ─── Compile-Time Constants ────────────────────────────────────────────────────

#define FW_VERSION          "1.0.1-webui"

#define EEPROM_MAGIC        0xA5    // Sentinel to detect initialised config
#define EEPROM_SIZE         256

#define DEFAULT_SERVER_PORT 1234
#define SEND_INTERVAL_MS    10000   // Periodic data push (ms)
#define PING_INTERVAL_MS    30000   // Blynk keep-alive PING (ms)
#define RECONNECT_INTERVAL  5000    // Retry TCP connection (ms)
#define HX711_SAMPLES       10      // Readings averaged per weight sample
#define TARE_SAMPLES         5      // Keep short on ESP8266 to avoid WDT resets
#define CAL_SAMPLES          5      // Keep short on ESP8266 to avoid WDT resets
#define ZERO_DEADBAND_G      15.0f  // Clamp tiny drift to zero
#define POUR_DELTA_G        30      // g change to classify as "pouring"
#define POUR_SETTLE_MS      2000    // ms of stability before pour ends
#define POUR_ARM_DELAY_MS   5000    // Ignore pour detection briefly after tare/boot

// ─── Blynk Command Codes ──────────────────────────────────────────────────────

#define BCMD_RESPONSE       0
#define BCMD_LOGIN          2
#define BCMD_PING           6
#define BCMD_HARDWARE_SYNC  16
#define BCMD_HARDWARE       20

// ─── Blynk Virtual Pin Numbers (as strings for body building) ────────────────
// Source: open-plaato-keg/lib/open_plaato_keg/plaato_data.ex

#define VP_PERCENT_LEFT    "48"
#define VP_IS_POURING      "49"
#define VP_AMOUNT_LEFT     "51"
#define VP_WEIGHT_RAW      "53"
#define VP_VOLUME_RAW      "54"
#define VP_POUR_VOL_RAW    "55"
#define VP_TEMPERATURE     "56"
#define VP_LAST_POUR       "59"
#define VP_TARE            "60"   // receive "1" → tare
#define VP_CALIBRATE       "61"   // receive grams → calibrate
#define VP_EMPTY_KEG       "62"   // receive "1" → capture current as empty
#define VP_BEER_NAME       "64"
#define VP_DATE            "67"
#define VP_UNIT            "71"   // "1"=metric  "2"=US
#define VP_WEIGHT_UNIT     "73"
#define VP_BEER_LEFT_UNIT  "74"
#define VP_MEASURE_UNIT    "75"   // "1"=weight  "2"=volume
#define VP_MAX_KEG_VOL     "76"   // litres
#define VP_WIFI_RSSI       "81"
#define VP_VOLUME_UNIT     "82"
#define VP_FIRMWARE_VER    "93"

// ─── Config Struct (persisted in EEPROM) ──────────────────────────────────────

struct Config {
  uint8_t  magic;
  char     server_host[64];
  uint16_t server_port;
  char     auth_token[33];     // 32-char hex + NUL
  float    cal_factor;         // HX711 scale factor (raw / gram)
  int32_t  tare_offset;        // HX711 raw offset after tare
  uint32_t empty_keg_g;        // Empty keg mass in grams
  uint32_t max_vol_ml;         // Full keg volume in mL
  char     beer_name[32];
};

// ─── Globals ──────────────────────────────────────────────────────────────────

HX711      scale;
WiFiClient client;
ESP8266WebServer webServer(80);
ESP8266HTTPUpdateServer httpUpdater;
Config     cfg;

// Blynk state
uint16_t blynk_msg_id   = 1;
bool     blynk_authed   = false;

// Timing
uint32_t last_send_ms   = 0;
uint32_t last_ping_ms   = 0;
uint32_t last_reconnect = 0;

// Pour tracking
float    last_stable_g  = 0.0f;
float    last_weight_g  = 0.0f;
bool     is_pouring     = false;
uint32_t pour_start_ms  = 0;
float    pour_start_g   = 0.0f;
bool     have_baseline  = false;
bool     ota_in_progress = false;
uint32_t pour_arm_ms    = 0;
float    instant_weight_g = 0.0f;

// Deferred actions from the web UI to avoid doing long HX711 work inside HTTP handlers
bool     pending_tare           = false;
bool     pending_set_empty_keg  = false;
bool     pending_calibrate      = false;
float    pending_known_g        = 0.0f;

// LED
uint32_t led_last_ms    = 0;
bool     led_state      = false;

// Button
uint32_t btn_down_ms    = 0;
bool     btn_held       = false;

// WiFiManager custom parameters (initialised in setup after config load)
WiFiManagerParameter *p_server = nullptr;
WiFiManagerParameter *p_port   = nullptr;
WiFiManagerParameter *p_token  = nullptr;
WiFiManagerParameter *p_cal    = nullptr;
WiFiManagerParameter *p_keg    = nullptr;
WiFiManagerParameter *p_name   = nullptr;

// ─── EEPROM Helpers ───────────────────────────────────────────────────────────

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);

  if (cfg.magic != EEPROM_MAGIC) {
    // First boot – apply defaults
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic        = EEPROM_MAGIC;
    cfg.server_port  = DEFAULT_SERVER_PORT;
    cfg.cal_factor   = 1.0f;
    cfg.tare_offset  = 0;
    cfg.empty_keg_g  = 4000;    // ~4 kg typical Cornelius keg
    cfg.max_vol_ml   = 19000;   // 19 L Cornelius keg
    strncpy(cfg.beer_name, "Beer", sizeof(cfg.beer_name) - 1);
  }
}

void saveConfig() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// ─── LED Control ──────────────────────────────────────────────────────────────

void ledSolid(bool on) {
  digitalWrite(LED_PIN, on ? LOW : HIGH);   // active LOW
}

void ledBlink(uint32_t interval_ms) {
  uint32_t now = millis();
  if (now - led_last_ms >= interval_ms) {
    led_last_ms = now;
    led_state   = !led_state;
    digitalWrite(LED_PIN, led_state ? LOW : HIGH);
  }
}

void ledFlash(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(80);
    yield();
    digitalWrite(LED_PIN, HIGH);
    delay(80);
    yield();
  }
}

// ─── OTA ──────────────────────────────────────────────────────────────────────

long readAverageRaw(uint8_t samples);

void logMemory(const char *tag) {
  Serial.printf("[Mem] %s free=%u frag=%u%% max=%u\n",
                tag,
                ESP.getFreeHeap(),
                ESP.getHeapFragmentation(),
                ESP.getMaxFreeBlockSize());
}

bool webAuthRequired() {
  return strlen(cfg.auth_token) > 0;
}

bool ensureWebAuth() {
  if (!webAuthRequired()) return true;
  if (webServer.authenticate("admin", cfg.auth_token)) return true;
  webServer.requestAuthentication();
  return false;
}

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

void handleWebState() {
  if (!ensureWebAuth()) return;

  float weight_g = readWeightGrams();
  long raw_adc = readAverageRaw(3);
  long raw_offset = scale.get_offset();
  long raw_delta = raw_adc - raw_offset;

  String json;
  json.reserve(340);
  json += F("{\"weight_g\":");
  json += String(weight_g, 1);
  json += F(",\"instant_weight_g\":");
  json += String(instant_weight_g, 1);
  json += F(",\"raw\":");
  json += String(raw_delta);
  json += F(",\"raw_adc\":");
  json += String(raw_adc);
  json += F(",\"raw_offset\":");
  json += String(raw_offset);
  json += F(",\"cal_factor\":");
  json += String(cfg.cal_factor, 4);
  json += F(",\"empty_keg_g\":");
  json += String(cfg.empty_keg_g);
  json += F(",\"max_vol_l\":");
  json += String((float)cfg.max_vol_ml / 1000.0f, 2);
  json += F(",\"beer_name\":\"");
  json += jsonEscape(String(cfg.beer_name));
  json += F("\",\"blynk\":\"");
  json += blynk_authed ? F("connected") : F("disconnected");
  json += F("\",\"ip\":\"");
  json += WiFi.localIP().toString();
  json += F("\",\"free_heap\":");
  json += String(ESP.getFreeHeap());
  json += F(",\"heap_frag\":");
  json += String(ESP.getHeapFragmentation());
  json += F(",\"max_block\":");
  json += String(ESP.getMaxFreeBlockSize());
  json += '}';

  webServer.send(200, "application/json", json);
}

void handleWebTare() {
  if (!ensureWebAuth()) return;
  pending_tare = true;
  webServer.send(202, "application/json", "{\"ok\":true,\"msg\":\"tare queued\"}");
}

void handleWebCalibrate() {
  if (!ensureWebAuth()) return;
  if (!webServer.hasArg("known_g")) {
    webServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"known_g required\"}");
    return;
  }

  float known_g = webServer.arg("known_g").toFloat();
  if (known_g <= 0) {
    webServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"known_g must be > 0\"}");
    return;
  }

  pending_known_g = known_g;
  pending_calibrate = true;
  webServer.send(202, "application/json", "{\"ok\":true,\"msg\":\"calibration queued\"}");
}

void handleWebSetEmptyKeg() {
  if (!ensureWebAuth()) return;

  if (webServer.hasArg("empty_g")) {
    float explicit_g = webServer.arg("empty_g").toFloat();
    if (explicit_g < 0) {
      webServer.send(400, "application/json", "{\"ok\":false,\"msg\":\"empty_g must be >= 0\"}");
      return;
    }
    cfg.empty_keg_g = (uint32_t)explicit_g;
    saveConfig();
    Serial.printf("[Scale] Empty keg weight set to %u g\n", cfg.empty_keg_g);
    webServer.send(200, "application/json", "{\"ok\":true}");
  } else {
    pending_set_empty_keg = true;
    webServer.send(202, "application/json", "{\"ok\":true,\"msg\":\"empty keg capture queued\"}");
  }
}

void handleWebResetSetup() {
  if (!ensureWebAuth()) return;

  webServer.send(200, "application/json", "{\"ok\":true,\"msg\":\"resetting to setup mode\"}");
  delay(100);

  Serial.println(F("[Web] Resetting WiFi settings and rebooting to setup mode"));
  WiFiManager wm;
  wm.resetSettings();
  delay(250);
  ESP.restart();
}

void handleWebRoot() {
  if (!ensureWebAuth()) return;

  String html;
  html.reserve(5000);
  html += F(
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Open Keg Scale</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;max-width:880px;margin:24px auto;padding:0 16px;background:#f6f7f9;color:#1f2937}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}"
    ".card{background:#fff;border:1px solid #d1d5db;border-radius:14px;padding:18px;box-shadow:0 2px 10px rgba(0,0,0,.05)}"
    "h1,h2{margin:0 0 12px}h1{font-size:1.8rem}h2{font-size:1rem}"
    "dl{display:grid;grid-template-columns:max-content 1fr;gap:8px 12px;margin:0}"
    "dt{font-weight:700}dd{margin:0}"
    "label{display:block;font-size:.85rem;font-weight:700;margin:10px 0 6px}"
    "input{width:100%;padding:10px 12px;border:1px solid #cbd5e1;border-radius:10px;font-size:1rem}"
    "button,a.button{display:inline-block;border:0;border-radius:10px;padding:10px 14px;background:#0f766e;color:#fff;text-decoration:none;font-weight:700;cursor:pointer}"
    "button.alt{background:#1d4ed8}button.warn{background:#9a3412}"
    ".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.mono{font-family:Consolas,monospace}"
    ".metric{font-size:2rem;font-weight:700}.muted{color:#64748b}"
    "#msg{display:none;margin-bottom:16px;padding:12px 14px;border-radius:10px;background:#ecfeff;border:1px solid #67e8f9}"
    "</style></head><body>"
    "<h1>Open Keg Scale</h1><div id='msg'></div><div class='grid'>"
    "<section class='card'><h2>Live Status</h2>"
    "<div class='metric mono' id='weight'>--.- g</div>"
    "<div class='muted'>Instant: <span class='mono' id='instant_weight'>--</span></div>"
    "<div class='muted'>Raw: <span class='mono' id='raw'>--</span></div>"
    "<div class='muted'>ADC: <span class='mono' id='raw_adc'>--</span></div>"
    "<div class='muted'>Offset: <span class='mono' id='raw_offset'>--</span></div>"
    "<div class='muted'>Blynk: <span id='blynk'>--</span></div>"
    "<dl><dt>IP</dt><dd class='mono' id='ip'>--</dd>"
    "<dt>Beer</dt><dd id='beer_name'>--</dd>"
    "<dt>Calibration</dt><dd class='mono' id='cal_factor'>--</dd>"
    "<dt>Empty Keg</dt><dd class='mono' id='empty_keg_g'>--</dd>"
    "<dt>Max Volume</dt><dd class='mono' id='max_vol_l'>--</dd></dl></section>"
    "<section class='card'><h2>Scale Actions</h2>"
    "<div class='row'><button onclick='postAction(\"/api/tare\")'>Tare Scale</button>"
    "<button class='alt' onclick='postAction(\"/api/empty-keg\")'>Capture Empty Keg</button></div>"
    "<label for='known_g'>Known Weight (grams)</label>"
    "<input id='known_g' type='number' step='0.1' placeholder='e.g. 2119'>"
    "<div class='row' style='margin-top:10px'><button class='warn' onclick='calibrate()'>Calibrate</button></div>"
    "<label for='empty_g'>Set Empty Keg Manually (grams)</label>"
    "<input id='empty_g' type='number' step='1' placeholder='optional manual override'>"
    "<div class='row' style='margin-top:10px'><button class='alt' onclick='setEmptyKegManual()'>Save Empty Keg</button></div>"
    "<div class='row' style='margin-top:18px'><button class='warn' onclick='resetToSetup()'>Reset WiFi / Enter Setup Mode</button></div>"
    "</section>"
    "<section class='card'><h2>Firmware</h2>"
    "<p class='muted'>Use the browser uploader for OTA updates.</p>"
    "<a class='button' href='/update'>Open Firmware Updater</a>"
    "<p class='muted' style='margin-top:12px'>Login: <span class='mono'>admin</span><br>Password: current auth token</p>"
    "</section></div>"
    "<script>"
    "async function refreshState(){const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)return;const s=await r.json();"
    "document.getElementById('weight').textContent=`${Number(s.weight_g).toFixed(1)} g`;"
    "document.getElementById('instant_weight').textContent=`${Number(s.instant_weight_g).toFixed(1)} g`;"
    "document.getElementById('raw').textContent=s.raw;"
    "document.getElementById('raw_adc').textContent=s.raw_adc;"
    "document.getElementById('raw_offset').textContent=s.raw_offset;"
    "document.getElementById('blynk').textContent=s.blynk;"
    "document.getElementById('ip').textContent=s.ip;"
    "document.getElementById('beer_name').textContent=s.beer_name;"
    "document.getElementById('cal_factor').textContent=Number(s.cal_factor).toFixed(4);"
    "document.getElementById('empty_keg_g').textContent=`${Number(s.empty_keg_g).toFixed(0)} g`;"
    "document.getElementById('max_vol_l').textContent=`${Number(s.max_vol_l).toFixed(2)} L`;}"
    "function showMsg(text){const el=document.getElementById('msg');el.textContent=text;el.style.display='block';setTimeout(()=>el.style.display='none',3500)}"
    "async function postAction(url,body){const opts={method:'POST'};if(body){opts.headers={'Content-Type':'application/x-www-form-urlencoded'};opts.body=body;}const r=await fetch(url,opts);const t=await r.text();if(!r.ok){showMsg(t);return;}showMsg('Saved');refreshState();}"
    "function calibrate(){const v=document.getElementById('known_g').value.trim();if(!v){showMsg('Enter known weight in grams');return;}postAction('/api/calibrate',`known_g=${encodeURIComponent(v)}`)}"
    "function setEmptyKegManual(){const v=document.getElementById('empty_g').value.trim();if(!v){showMsg('Enter empty keg grams');return;}postAction('/api/empty-keg',`empty_g=${encodeURIComponent(v)}`)}"
    "function resetToSetup(){if(!confirm('Reset WiFi settings and reboot into setup mode?'))return;postAction('/api/reset-setup')}"
    "refreshState();setInterval(refreshState,2000);"
    "</script></body></html>");

  webServer.send(200, "text/html", html);
}

void setupWebOTA() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/state", HTTP_GET, handleWebState);
  webServer.on("/api/tare", HTTP_POST, handleWebTare);
  webServer.on("/api/calibrate", HTTP_POST, handleWebCalibrate);
  webServer.on("/api/empty-keg", HTTP_POST, handleWebSetEmptyKeg);
  webServer.on("/api/reset-setup", HTTP_POST, handleWebResetSetup);

  if (strlen(cfg.auth_token) > 0) {
    httpUpdater.setup(&webServer, "/update", "admin", cfg.auth_token);
  } else {
    httpUpdater.setup(&webServer, "/update");
    Serial.println(F("[WebOTA] WARNING: /update is not password protected"));
  }

  webServer.begin();
  Serial.printf("[WebOTA] Ready. URL=http://%s/ User=%s Password=%s\n",
                WiFi.localIP().toString().c_str(),
                strlen(cfg.auth_token) > 0 ? "admin" : "(none)",
                strlen(cfg.auth_token) > 0 ? "(auth token)" : "(none)");
}

void setupOTA() {
  char ota_host[32];
  snprintf(ota_host, sizeof(ota_host), "keg-scale-%06X", ESP.getChipId());

  ArduinoOTA.setHostname(ota_host);
  if (strlen(cfg.auth_token) > 0) {
    ArduinoOTA.setPassword(cfg.auth_token);
  }

  ArduinoOTA.onStart([]() {
    ota_in_progress = true;
    blynk_authed = false;
    client.stop();
    ledSolid(true);
    Serial.println(F("[OTA] Update starting"));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println(F("\n[OTA] Update complete"));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static uint8_t last_pct = 255;
    uint8_t pct = (uint8_t)((progress * 100U) / total);
    if (pct == 100 || (pct % 10 == 0 && pct != last_pct)) {
      Serial.printf("[OTA] Progress: %u%%\n", pct);
      last_pct = pct;
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    ota_in_progress = false;
    ledSolid(false);
    Serial.printf("[OTA] Error[%u]\n", (unsigned)error);
  });

  ArduinoOTA.begin();
  Serial.printf("[OTA] Ready. Hostname=%s Password=%s\n",
                ota_host,
                strlen(cfg.auth_token) ? "(auth token)" : "(none)");
  ledFlash(2);
}

// ─── Blynk Protocol Helpers ───────────────────────────────────────────────────

/*
 * All Blynk packets:   [cmd:u8][msg_id:u16be][length:u16be][body:bytes]
 * RESPONSE exception:  [0x00  ][msg_id:u16be][status:u16be]   (no body)
 */

void blynkSendRaw(uint8_t cmd, const uint8_t *body, uint16_t body_len) {
  uint8_t pkt[96];
  uint16_t pkt_len = body_len + 5;

  pkt[0] = cmd;
  pkt[1] = (blynk_msg_id >> 8) & 0xFF;
  pkt[2] =  blynk_msg_id       & 0xFF;
  pkt[3] = (body_len   >> 8)   & 0xFF;
  pkt[4] =  body_len           & 0xFF;

  if (body_len > 0 && body != nullptr) {
    memcpy(pkt + 5, body, body_len);
  }

  client.write(pkt, pkt_len);
  if (++blynk_msg_id == 0) blynk_msg_id = 1;
}

// Send a RESPONSE ack (used to acknowledge incoming PING / HARDWARE)
void blynkAck(uint16_t msg_id, uint16_t status = 200) {
  uint8_t pkt[5];
  pkt[0] = BCMD_RESPONSE;
  pkt[1] = (msg_id >> 8) & 0xFF;
  pkt[2] =  msg_id       & 0xFF;
  pkt[3] = (status >> 8) & 0xFF;
  pkt[4] =  status       & 0xFF;
  client.write(pkt, 5);
}

// Send   vw <pin> <value>   as a HARDWARE command
void blynkSendPin(const char *pin, const char *value) {
  // Body: "vw\0<pin>\0<value>"
  char body[80];
  int  len = 0;
  body[len++] = 'v';
  body[len++] = 'w';
  body[len++] = '\0';
  len += snprintf(body + len, sizeof(body) - len - 1, "%s", pin);
  body[len++] = '\0';
  len += snprintf(body + len, sizeof(body) - len, "%s", value);
  blynkSendRaw(BCMD_HARDWARE, (uint8_t *)body, (uint16_t)len);
}

void blynkSendPin(const char *pin, float value, uint8_t decimals = 2) {
  char buf[20];
  dtostrf(value, 1, decimals, buf);
  blynkSendPin(pin, buf);
}

void blynkSendPin(const char *pin, int32_t value) {
  char buf[14];
  snprintf(buf, sizeof(buf), "%ld", (long)value);
  blynkSendPin(pin, buf);
}

// Parse a body of the form "vw\0<pin>\0<value>" and return pointers.
// Returns false if body is too short or format is wrong.
bool parseHardwareBody(const char *body, uint16_t len,
                       const char **kind_out,
                       const char **pin_out,
                       const char **val_out)
{
  *kind_out = body;
  *pin_out  = nullptr;
  *val_out  = nullptr;

  for (uint16_t i = 0; i < len; i++) {
    if (body[i] == '\0') {
      if (!*pin_out) {
        *pin_out = body + i + 1;
      } else if (!*val_out) {
        *val_out = body + i + 1;
        return true;
      }
    }
  }
  // val may be absent (e.g. tare "1" might be the only token after pin)
  return (*pin_out != nullptr);
}

// ─── Scale Functions ──────────────────────────────────────────────────────────

float readWeightGrams() {
  if (!scale.is_ready()) return last_weight_g;
  float w = scale.get_units(HX711_SAMPLES);
  if (isnan(w) || isinf(w)) return last_weight_g;
  instant_weight_g = w;
  if (fabsf(w) < ZERO_DEADBAND_G) w = 0.0f;
  return w;
}

long readAverageRaw(uint8_t samples) {
  if (samples == 0) return scale.read();

  int64_t sum = 0;
  uint8_t count = 0;

  while (count < samples) {
    uint32_t wait_start = millis();
    while (!scale.is_ready()) {
      if (millis() - wait_start > 1000) {
        break;
      }
      yield();
      delay(1);
    }

    if (!scale.is_ready()) {
      break;
    }

    sum += scale.read();
    count++;
    yield();
    delay(1);
  }

  if (count == 0) return 0;
  return (long)(sum / count);
}

void doTare() {
  ledSolid(true);
  Serial.println(F("[Scale] Taring..."));
  long new_offset = readAverageRaw(TARE_SAMPLES);
  if (new_offset == 0) {
    Serial.println(F("[Scale] Tare failed: no HX711 data"));
    ledSolid(false);
    return;
  }

  logMemory("before tare save");
  scale.set_offset(new_offset);
  cfg.tare_offset = new_offset;
  saveConfig();
  last_stable_g = 0;
  last_weight_g = 0;
  instant_weight_g = 0;
  have_baseline = false;
  is_pouring = false;
  pour_arm_ms = millis() + POUR_ARM_DELAY_MS;
  Serial.printf("[Scale] Tare done. Offset=%ld\n", (long)cfg.tare_offset);
  logMemory("after tare");
  ledFlash(3);
  ledSolid(false);
}

void doSetEmptyKeg(float current_g) {
  // Captures the current TOTAL reading as the empty keg weight
  cfg.empty_keg_g = (uint32_t)(current_g > 0 ? current_g : 0);
  saveConfig();
  Serial.printf("[Scale] Empty keg weight set to %u g\n", cfg.empty_keg_g);
  ledFlash(2);
}

void doCalibrateWithKnownWeight(float known_g) {
  // Place known_g grams on the scale before calling this.
  // Reads the raw HX711 value and back-calculates the scale factor.
  if (known_g <= 0) return;
  long raw = readAverageRaw(CAL_SAMPLES) - scale.get_offset();
  if (raw == 0) {
    Serial.println(F("[Scale] Calibration failed: raw=0"));
    return;
  }
  cfg.cal_factor = (float)raw / known_g;
  scale.set_scale(cfg.cal_factor);
  logMemory("before cal save");
  saveConfig();
  have_baseline = false;
  instant_weight_g = 0;
  is_pouring = false;
  pour_arm_ms = millis() + POUR_ARM_DELAY_MS;
  Serial.printf("[Scale] Cal factor=%.4f (raw=%ld known=%.1f g)\n",
                cfg.cal_factor, raw, known_g);
  logMemory("after cal");
  ledFlash(4);
}

// ─── Data Publishing ──────────────────────────────────────────────────────────

void publishKegData(float weight_g) {
  // Liquid mass = total mass - empty keg mass
  float liquid_g = weight_g - (float)cfg.empty_keg_g;
  if (liquid_g < 0) liquid_g = 0;

  // Volume (assuming ~1.0 g/mL for beer; close enough for tracking)
  float vol_L = liquid_g / 1000.0f;

  float max_vol_L = (float)cfg.max_vol_ml / 1000.0f;
  if (max_vol_L <= 0) max_vol_L = 19.0f;

  float pct = (vol_L / max_vol_L) * 100.0f;
  if (pct > 100.0f) pct = 100.0f;
  if (pct <   0.0f) pct = 0.0f;

  blynkSendPin(VP_WEIGHT_RAW,   weight_g, 0);
  blynkSendPin(VP_VOLUME_RAW,   vol_L,    2);
  blynkSendPin(VP_AMOUNT_LEFT,  vol_L,    2);
  blynkSendPin(VP_PERCENT_LEFT, pct,      1);
  blynkSendPin(VP_IS_POURING,   is_pouring ? "1" : "0");
}

void publishStaticPins() {
  blynkSendPin(VP_UNIT,         "1");               // metric
  blynkSendPin(VP_MEASURE_UNIT, "2");               // report as volume
  blynkSendPin(VP_WEIGHT_UNIT,  "kg");
  blynkSendPin(VP_BEER_LEFT_UNIT,"L");
  blynkSendPin(VP_VOLUME_UNIT,  "L");
  blynkSendPin(VP_MAX_KEG_VOL,
               String((float)cfg.max_vol_ml / 1000.0f, 2).c_str());
  blynkSendPin(VP_BEER_NAME,    cfg.beer_name);
  blynkSendPin(VP_FIRMWARE_VER, FW_VERSION);

  char rssi[8];
  snprintf(rssi, sizeof(rssi), "%d", (int)WiFi.RSSI());
  blynkSendPin(VP_WIFI_RSSI, rssi);
}

// Handle a HARDWARE_SYNC request (server asks device to resend specific pins).
// body format: "vr\0<pin1>\0<pin2>..."
void handleHardwareSync(const char *body, uint16_t len) {
  // Re-publish everything — simpler than parsing individual pins
  float w = readWeightGrams();
  publishKegData(w);
  publishStaticPins();
}

// Handle a HARDWARE write FROM the server (commands to the device).
void handleIncomingHardware(const char *body, uint16_t len, uint16_t msg_id) {
  blynkAck(msg_id);

  const char *kind, *pin, *val;
  if (!parseHardwareBody(body, len, &kind, &pin, &val)) return;
  if (strncmp(kind, "vw", 2) != 0) return;   // only handle writes
  if (!pin) return;

  const char *v = val ? val : "";

  if (strcmp(pin, VP_TARE) == 0) {
    if (strcmp(v, "1") == 0) doTare();

  } else if (strcmp(pin, VP_CALIBRATE) == 0) {
    float kg = atof(v);
    if (kg > 0) doCalibrateWithKnownWeight(kg);

  } else if (strcmp(pin, VP_EMPTY_KEG) == 0) {
    if (strcmp(v, "1") == 0) {
      doSetEmptyKeg(readWeightGrams());
    } else {
      float explicit_g = atof(v);
      if (explicit_g >= 0) {
        cfg.empty_keg_g = (uint32_t)explicit_g;
        saveConfig();
        Serial.printf("[Scale] Empty keg weight set to %u g\n", cfg.empty_keg_g);
      }
    }

  } else if (strcmp(pin, VP_MAX_KEG_VOL) == 0) {
    float l = atof(v);
    if (l > 0) {
      cfg.max_vol_ml = (uint32_t)(l * 1000.0f);
      saveConfig();
      Serial.printf("[Config] Max keg volume = %.1f L\n", l);
    }

  } else if (strcmp(pin, VP_BEER_NAME) == 0) {
    strncpy(cfg.beer_name, v, sizeof(cfg.beer_name) - 1);
    cfg.beer_name[sizeof(cfg.beer_name) - 1] = '\0';
    saveConfig();
    Serial.printf("[Config] Beer name = %s\n", cfg.beer_name);

  } else if (strcmp(pin, VP_UNIT) == 0) {
    // "1"=metric "2"=US — we always work in metric internally; just ack
  } else if (strcmp(pin, VP_MEASURE_UNIT) == 0) {
    // "1"=weight "2"=volume — we report both regardless
  }
}

// ─── Blynk Receive Loop ───────────────────────────────────────────────────────

void blynkProcessIncoming() {
  while (client.available() >= 5) {
    uint8_t hdr[5];
    client.peekBytes(hdr, 5);

    uint8_t  cmd    = hdr[0];
    uint16_t msg_id = ((uint16_t)hdr[1] << 8) | hdr[2];
    uint16_t length = ((uint16_t)hdr[3] << 8) | hdr[4];

    uint16_t packet_len = (cmd == BCMD_RESPONSE) ? 5 : (uint16_t)(5 + length);
    if (client.available() < packet_len) {
      return;
    }

    client.readBytes(hdr, 5);

    // For RESPONSE packets the length field IS the status code, body = 0 bytes
    if (cmd == BCMD_RESPONSE) {
      if (length == 200 && !blynk_authed) {
        blynk_authed = true;
        Serial.println(F("[Blynk] Authenticated OK"));
        float w = readWeightGrams();
        publishKegData(w);
        publishStaticPins();
      }
      continue;
    }

    // Read body (guard against oversized payloads)
    char body[128] = {0};
    if (length > 0) {
      if (length < sizeof(body)) {
        client.readBytes(body, length);
      } else {
        for (uint16_t i = 0; i < length; i++) client.read();  // discard
        continue;
      }
    }

    switch (cmd) {
      case BCMD_PING:
        blynkAck(msg_id);
        break;

      case BCMD_HARDWARE:
        handleIncomingHardware(body, length, msg_id);
        break;

      case BCMD_HARDWARE_SYNC:
        blynkAck(msg_id);
        handleHardwareSync(body, length);
        break;

      default:
        blynkAck(msg_id);   // ack unknown commands gracefully
        break;
    }
  }
}

// ─── WiFiManager Callback ─────────────────────────────────────────────────────

void saveParamsCallback() {
  strncpy(cfg.server_host, p_server->getValue(), sizeof(cfg.server_host) - 1);
  cfg.server_host[sizeof(cfg.server_host) - 1] = '\0';

  uint16_t port = (uint16_t)atoi(p_port->getValue());
  cfg.server_port = (port > 0) ? port : DEFAULT_SERVER_PORT;

  strncpy(cfg.auth_token, p_token->getValue(), sizeof(cfg.auth_token) - 1);
  cfg.auth_token[sizeof(cfg.auth_token) - 1] = '\0';

  float cal_factor = atof(p_cal->getValue());
  if (cal_factor != 0.0f && !isnan(cal_factor) && !isinf(cal_factor)) {
    cfg.cal_factor = cal_factor;
  }

  float max_vol = atof(p_keg->getValue());
  if (max_vol > 0) cfg.max_vol_ml = (uint32_t)(max_vol * 1000.0f);

  strncpy(cfg.beer_name, p_name->getValue(), sizeof(cfg.beer_name) - 1);
  cfg.beer_name[sizeof(cfg.beer_name) - 1] = '\0';

  saveConfig();
  Serial.println(F("[WiFiManager] Config saved"));
}

void setupWiFi() {
  // Build WiFiManager parameter objects with current config as defaults
  char port_str[8], cal_str[20], keg_str[8];
  snprintf(port_str, sizeof(port_str), "%u", cfg.server_port);
  snprintf(cal_str,  sizeof(cal_str),  "%.4f", cfg.cal_factor);
  snprintf(keg_str,  sizeof(keg_str),  "%.1f", (float)cfg.max_vol_ml / 1000.0f);

  p_server = new WiFiManagerParameter("server", "Plaato Server Host",    cfg.server_host, 64);
  p_port   = new WiFiManagerParameter("port",   "Server Port",           port_str,         6);
  p_token  = new WiFiManagerParameter("token",  "Auth Token (32 hex)",   cfg.auth_token,  33);
  p_cal    = new WiFiManagerParameter("cal",    "Calibration Factor",    cal_str,         20);
  p_keg    = new WiFiManagerParameter("keg",    "Max Keg Volume (L)",    keg_str,          8);
  p_name   = new WiFiManagerParameter("name",   "Beer Name",             cfg.beer_name,   32);

  WiFiManager wm;
  wm.addParameter(p_server);
  wm.addParameter(p_port);
  wm.addParameter(p_token);
  wm.addParameter(p_cal);
  wm.addParameter(p_keg);
  wm.addParameter(p_name);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setConfigPortalTimeout(180);   // 3-minute portal timeout

  if (!wm.autoConnect("KegScale-Setup")) {
    Serial.println(F("[WiFi] autoConnect failed – restarting"));
    delay(2000);
    ESP.restart();
  }

  Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─── Blynk Connection ─────────────────────────────────────────────────────────

bool connectBlynk() {
  if (strlen(cfg.server_host) == 0 || strlen(cfg.auth_token) == 0) {
    Serial.println(F("[Blynk] Server or token not configured. "
                     "Connect to AP \"KegScale-Setup\" to configure."));
    return false;
  }

  Serial.printf("[Blynk] Connecting to %s:%u ...\n",
                cfg.server_host, cfg.server_port);

  if (!client.connect(cfg.server_host, cfg.server_port)) {
    Serial.println(F("[Blynk] TCP connect failed"));
    return false;
  }

  // Send LOGIN (cmd=2) with auth token as body
  blynk_authed = false;
  blynk_msg_id = 1;
  blynkSendRaw(BCMD_LOGIN,
               (const uint8_t *)cfg.auth_token,
               (uint16_t)strlen(cfg.auth_token));

  Serial.println(F("[Blynk] LOGIN sent – awaiting auth response"));
  return true;
}

// ─── Button Handling ──────────────────────────────────────────────────────────

void handleButton() {
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  if (pressed && !btn_held) {
    btn_down_ms = millis();
    btn_held    = true;
  }

  if (!pressed && btn_held) {
    uint32_t held_ms = millis() - btn_down_ms;
    btn_held = false;

    if (held_ms > 4000) {
      // Long press: wipe WiFi config and restart
      Serial.println(F("[Button] Long press – wiping WiFi credentials"));
      WiFiManager wm;
      wm.resetSettings();
      delay(500);
      ESP.restart();

    } else if (held_ms > 50) {
      // Short press: tare
      doTare();
    }
  }
}

// ─── Pour Detection ───────────────────────────────────────────────────────────

void updatePourState(float weight_g) {
  if (!have_baseline) {
    last_stable_g = weight_g;
    last_weight_g = weight_g;
    have_baseline = true;
    return;
  }

  if ((int32_t)(millis() - pour_arm_ms) < 0) {
    last_stable_g = weight_g;
    last_weight_g = weight_g;
    is_pouring = false;
    return;
  }

  float delta = weight_g - last_stable_g;

  if (!is_pouring) {
    if (delta < -POUR_DELTA_G) {
      // Weight dropped — pour started
      is_pouring   = true;
      pour_start_g = last_stable_g;
      pour_start_ms = millis();
      Serial.printf("[Pour] Started (delta=%.0f g)\n", delta);
    }
  } else {
    // Currently pouring – check for stabilisation
    float delta_recent = fabsf(weight_g - last_weight_g);
    if (delta_recent < (POUR_DELTA_G / 3.0f)) {
      // Weight has been stable briefly — has the settle window elapsed?
      if (millis() - pour_start_ms > POUR_SETTLE_MS) {
        float poured_g = pour_start_g - weight_g;
        if (poured_g > 0) {
          float poured_L = poured_g / 1000.0f;
          Serial.printf("[Pour] Ended. Volume=%.2f L\n", poured_L);
          char buf[12];
          dtostrf(poured_L, 1, 2, buf);
          if (blynk_authed) {
            blynkSendPin(VP_LAST_POUR,    buf);
            blynkSendPin(VP_POUR_VOL_RAW, buf);
          }
        }
        is_pouring    = false;
        last_stable_g = weight_g;
      }
    } else {
      // Still moving – reset settle timer
      pour_start_ms = millis();
    }
  }

  // Update stable reference when weight is not pouring and stable
  if (!is_pouring && fabsf(delta) < (POUR_DELTA_G / 3.0f)) {
    last_stable_g = weight_g;
  }

  last_weight_g = weight_g;
}

// ─── setup() ──────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n[Boot] Open Keg Scale v" FW_VERSION));

  pinMode(LED_PIN,    OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  ledSolid(false);

  loadConfig();
  Serial.printf("[Config] Server=%s:%u  Token=%s  CalFactor=%.4f\n",
                cfg.server_host, cfg.server_port,
                strlen(cfg.auth_token) ? "(set)" : "(not set)",
                cfg.cal_factor);

  // Initialise HX711
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  scale.set_gain(128);   // Channel A, 128× gain
  scale.set_scale(cfg.cal_factor);
  scale.set_offset(cfg.tare_offset);
  instant_weight_g = 0;
  pour_arm_ms = millis() + POUR_ARM_DELAY_MS;
  Serial.printf("[Scale] HX711 ready.  Factor=%.4f  Offset=%ld\n",
                cfg.cal_factor, (long)cfg.tare_offset);

  if (cfg.cal_factor == 1.0f) {
    Serial.println(F("[Scale] WARNING: Scale not calibrated. "
                     "Use the server UI to calibrate with a known weight."));
  }

  setupWiFi();
  logMemory("after wifi");
  setupOTA();
  setupWebOTA();
  logMemory("after web");
  connectBlynk();
}

// ─── loop() ───────────────────────────────────────────────────────────────────

void loop() {
  handleButton();
  ArduinoOTA.handle();
  webServer.handleClient();

  if (ota_in_progress) {
    return;
  }

  if (pending_tare) {
    pending_tare = false;
    doTare();
  }

  if (pending_calibrate) {
    pending_calibrate = false;
    doCalibrateWithKnownWeight(pending_known_g);
    pending_known_g = 0.0f;
  }

  if (pending_set_empty_keg) {
    pending_set_empty_keg = false;
    doSetEmptyKeg(readWeightGrams());
  }

  // ── WiFi watchdog ──
  if (WiFi.status() != WL_CONNECTED) {
    blynk_authed = false;
    client.stop();
    ledBlink(100);
    return;
  }

  // ── Blynk reconnect ──
  if (!client.connected()) {
    blynk_authed = false;
    ledBlink(250);
    if (millis() - last_reconnect >= RECONNECT_INTERVAL) {
      last_reconnect = millis();
      connectBlynk();
    }
    return;
  }

  // ── Process incoming packets ──
  blynkProcessIncoming();

  if (!blynk_authed) {
    ledBlink(250);
    return;
  }

  // ── Read weight ──
  float weight_g = readWeightGrams();
  bool  was_pouring = is_pouring;
  updatePourState(weight_g);

  // ── Push data ──
  uint32_t now = millis();
  bool pour_changed = (is_pouring != was_pouring);

  if (pour_changed || (now - last_send_ms >= SEND_INTERVAL_MS)) {
    last_send_ms = now;
    publishKegData(weight_g);
  }

  // ── Keep-alive PING ──
  if (now - last_ping_ms >= PING_INTERVAL_MS) {
    last_ping_ms = now;
    blynkSendRaw(BCMD_PING, nullptr, 0);
  }

  // Slow blink = connected & running
  ledBlink(500);
}
