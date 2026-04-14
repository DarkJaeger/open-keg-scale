/*
 * Transfer Keg Firmware — Wemos D1 Mini (ESP8266)
 * ─────────────────────────────────────────────────
 * First boot (or no saved config):
 *   1. Broadcasts AP "TransferScale-Setup"  (pw: "scaleme")
 *   2. Connect → browse to 192.168.4.1
 *   3. Configure WiFi, server, and optionally run the Calibration wizard
 *   4. Save & Reboot — device connects as STA
 *
 * Re-enter setup: hold D3/FLASH at boot for 3 s, or send 'x' via Serial.
 *
 * Calibration API (used by the portal wizard):
 *   POST /cal/tare          — zero the scale (nothing on platform)
 *   POST /cal/measure       — body: {"knownKg": 10.0}
 *                             returns {"raw": 123456, "factor": -432.1, "readKg": 10.01}
 *   POST /cal/apply         — body: {"factor": -432.1}
 *                             writes factor to cfg and saves EEPROM
 */

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <HX711.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ─────────────────────────────────────────────
//  HARDWARE CONSTANTS
// ─────────────────────────────────────────────
#define HX711_DOUT_PIN   14   // D5
#define HX711_SCK_PIN    12   // D6
#define OLED_SDA_PIN      4   // D2
#define OLED_SCL_PIN      5   // D1
#define SETUP_BTN_PIN     0   // D3 / GPIO0 (FLASH button — active LOW)
#define TARE_BTN_PIN     13   // D7 — momentary button to GND, active LOW

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT    64
#define OLED_RESET       -1
#define OLED_ADDRESS   0x3C

#define WEIGH_INTERVAL_MS    1000
#define POST_INTERVAL_MS     5000
#define GET_INTERVAL_MS     30000
#define SCALE_SAMPLES           5
#define CAL_SAMPLES            20   // more samples for calibration accuracy

// ─────────────────────────────────────────────
//  AP / PORTAL CONSTANTS
// ─────────────────────────────────────────────
#define AP_SSID     "TransferScale-Setup"
#define AP_PASSWORD "scaleme"
#define DNS_PORT    53
static const IPAddress AP_IP(192, 168, 4, 1);

// ─────────────────────────────────────────────
//  EEPROM
// ─────────────────────────────────────────────
#define EEPROM_SIZE       512
#define EEPROM_MAGIC_VAL  0xA5
#define EEPROM_ADDR_MAGIC   0
#define EEPROM_ADDR_CFG     1

struct Config {
    char  wifiSSID[64];
    char  wifiPassword[64];
    char  serverBaseURL[128];
    char  scaleID[32];
    float calibrationFactor;
    float defaultEmptyKg;
    float defaultTargetKg;
};

Config cfg;

// ─────────────────────────────────────────────
//  OBJECTS
// ─────────────────────────────────────────────
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HX711            scale;
WiFiClient       wifiClient;
HTTPClient       http;
ESP8266WebServer server(80);
DNSServer        dnsServer;

// ─────────────────────────────────────────────
//  RUNTIME STATE
// ─────────────────────────────────────────────
float         currentWeightKg = 0.0f;
float         emptyKegKg      = 3.8f;
float         targetKg        = 12.5f;
unsigned long lastWeighTime   = 0;
unsigned long lastPostTime    = 0;
unsigned long lastGetTime     = 0;
bool          wifiConnected   = false;
bool          scaleReady      = false;
bool          calTareDone     = false;   // tracks wizard step state
float         calPendingFactor = 0.0f;  // factor calculated but not yet applied

// ─────────────────────────────────────────────
//  EEPROM HELPERS
// ─────────────────────────────────────────────
void saveConfig() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VAL);
    const uint8_t* p = (const uint8_t*)&cfg;
    for (size_t i = 0; i < sizeof(cfg); i++)
        EEPROM.write(EEPROM_ADDR_CFG + i, p[i]);
    EEPROM.commit();
    EEPROM.end();
    Serial.println("[EEPROM] saved");
}

bool loadConfig() {
    EEPROM.begin(EEPROM_SIZE);
    if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_VAL) { EEPROM.end(); return false; }
    uint8_t* p = (uint8_t*)&cfg;
    for (size_t i = 0; i < sizeof(cfg); i++)
        p[i] = EEPROM.read(EEPROM_ADDR_CFG + i);
    EEPROM.end();
    Serial.println("[EEPROM] loaded");
    return true;
}

void setDefaultConfig() {
    strncpy(cfg.wifiSSID,      "",                          sizeof(cfg.wifiSSID)      - 1);
    strncpy(cfg.wifiPassword,  "",                          sizeof(cfg.wifiPassword)  - 1);
    strncpy(cfg.serverBaseURL, "http://192.168.1.100:3000", sizeof(cfg.serverBaseURL) - 1);
    strncpy(cfg.scaleID,       "scale1",                   sizeof(cfg.scaleID)       - 1);
    cfg.calibrationFactor = -430.0f;
    cfg.defaultEmptyKg    = 4.028f;
    cfg.defaultTargetKg   = 19.15f;
}

// ─────────────────────────────────────────────
//  OLED HELPERS
// ─────────────────────────────────────────────
void drawProgressBar(int x, int y, int w, int h, float pct) {
    display.drawRect(x, y, w, h, SH110X_WHITE);
    int fill = (int)((float)(w - 2) * constrain(pct, 0.0f, 1.0f));
    if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SH110X_WHITE);
}

void showMessage(const char* l1, const char* l2 = nullptr, const char* l3 = nullptr) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 10); display.println(l1);
    if (l2) { display.setCursor(0, 26); display.println(l2); }
    if (l3) { display.setCursor(0, 42); display.println(l3); }
    display.display();
}

void renderSetupScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0,  0); display.println(F("=== SETUP MODE ==="));
    display.setCursor(0, 14); display.println(F("WiFi: TransferScale"));
    display.setCursor(0, 24); display.println(F("      -Setup"));
    display.setCursor(0, 36); display.println(F("Pass: scaleme"));
    display.setCursor(0, 50); display.println(F("http://192.168.4.1"));
    display.display();
}

void renderDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);   display.print(F("TRANSFER SCALE"));
    display.setCursor(110, 0); display.print(wifiConnected ? F("[W]") : F("[ ]"));

    display.setTextSize(2);
    display.setCursor(0, 18);
    char buf[12];
    dtostrf(currentWeightKg, 5, 2, buf);
    display.print(buf);
    display.setTextSize(1);
    display.print(F(" kg"));

    float netTgt = targetKg - emptyKegKg;
    float pct    = (netTgt > 0.f) ? constrain((currentWeightKg - emptyKegKg) / netTgt, 0.f, 1.f) : 0.f;
    drawProgressBar(0, 38, 90, 8, pct);
    char pb[6]; snprintf(pb, sizeof(pb), "%3d%%", (int)(pct * 100.f));
    display.setCursor(94, 38); display.print(pb);

    display.setCursor(0, 50);  display.print(F("Tgt:"));
    dtostrf(targetKg, 5, 1, buf); display.print(buf); display.print(F("kg"));
    display.setCursor(68, 50); display.print(F("Emp:"));
    dtostrf(emptyKegKg, 4, 1, buf); display.print(buf);

    display.display();
}

// ─────────────────────────────────────────────
//  PORTAL HTML  (stored in PROGMEM)
// ─────────────────────────────────────────────
static const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Transfer Scale Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0f1117;color:#e2e8f0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
.card{background:#1e2130;border:1px solid #2d3352;border-radius:12px;padding:28px 24px;width:100%;max-width:460px}
h1{font-size:1.2rem;font-weight:700;color:#7dd3fc;margin-bottom:4px}
.sub{font-size:.78rem;color:#64748b;margin-bottom:20px}
/* Tabs */
.tabs{display:flex;gap:4px;margin-bottom:24px;border-bottom:1px solid #2d3352;padding-bottom:0}
.tab{padding:8px 16px;font-size:.82rem;font-weight:600;color:#64748b;cursor:pointer;border-radius:6px 6px 0 0;border:1px solid transparent;border-bottom:none;margin-bottom:-1px;transition:all .15s}
.tab.active{background:#1e2130;border-color:#2d3352;color:#7dd3fc}
.tab:hover:not(.active){color:#94a3b8}
.pane{display:none}.pane.active{display:block}
/* Form */
.section{font-size:.68rem;font-weight:700;color:#475569;text-transform:uppercase;letter-spacing:.08em;margin-bottom:10px}
hr{border:none;border-top:1px solid #2d3352;margin:20px 0}
.group{margin-bottom:14px}
label{display:block;font-size:.74rem;font-weight:600;color:#94a3b8;margin-bottom:5px;text-transform:uppercase;letter-spacing:.05em}
input{width:100%;padding:10px 12px;background:#0f1117;border:1px solid #2d3352;border-radius:8px;color:#e2e8f0;font-size:.9rem;outline:none;transition:border-color .2s}
input:focus{border-color:#7dd3fc}
input:read-only{opacity:.5;cursor:not-allowed}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.hint{font-size:.69rem;color:#475569;margin-top:4px}
button{width:100%;padding:11px;background:#3b82f6;color:#fff;border:none;border-radius:8px;font-size:.9rem;font-weight:600;cursor:pointer;margin-top:8px;transition:background .2s}
button:hover:not(:disabled){background:#2563eb}
button:disabled{background:#334155;cursor:not-allowed;opacity:.6}
button.secondary{background:#334155}
button.secondary:hover:not(:disabled){background:#3f4f6a}
button.success{background:#16a34a}
button.success:hover:not(:disabled){background:#15803d}
.flash{display:none;padding:10px 14px;border-radius:8px;font-size:.84rem;margin-bottom:16px}
.flash.ok{display:block;background:#052e16;border:1px solid #16a34a;color:#4ade80}
.flash.err{display:block;background:#1c0a0a;border:1px solid #dc2626;color:#f87171}
.flash.info{display:block;background:#0c1a2e;border:1px solid #3b82f6;color:#7dd3fc}
/* Calibration wizard */
.step{display:none;animation:fadein .25s}
.step.active{display:block}
@keyframes fadein{from{opacity:0;transform:translateY(4px)}to{opacity:1;transform:none}}
.step-header{display:flex;align-items:center;gap:10px;margin-bottom:16px}
.step-num{width:28px;height:28px;border-radius:50%;background:#3b82f6;color:#fff;display:flex;align-items:center;justify-content:center;font-size:.8rem;font-weight:700;flex-shrink:0}
.step-title{font-size:1rem;font-weight:600;color:#e2e8f0}
.step-body{color:#94a3b8;font-size:.88rem;line-height:1.6;margin-bottom:16px}
.result-box{background:#0f1117;border:1px solid #2d3352;border-radius:8px;padding:14px;margin:14px 0}
.result-row{display:flex;justify-content:space-between;align-items:center;padding:4px 0;font-size:.85rem}
.result-row .label{color:#64748b}
.result-row .value{color:#7dd3fc;font-weight:600;font-family:monospace}
.result-row .value.good{color:#4ade80}
.result-row .value.warn{color:#facc15}
.spinner{display:inline-block;width:14px;height:14px;border:2px solid #334155;border-top-color:#7dd3fc;border-radius:50%;animation:spin .7s linear infinite;margin-right:6px;vertical-align:middle}
@keyframes spin{to{transform:rotate(360deg)}}
.progress-steps{display:flex;gap:6px;margin-bottom:20px}
.ps{height:3px;flex:1;border-radius:2px;background:#2d3352;transition:background .3s}
.ps.done{background:#3b82f6}
.ps.active{background:#7dd3fc}
/* Live weight widget */
.live-weight{background:#0f1117;border:1px solid #2d3352;border-radius:10px;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;margin-bottom:20px}
.lw-label{font-size:.72rem;font-weight:700;color:#475569;text-transform:uppercase;letter-spacing:.08em}
.lw-value{font-size:1.6rem;font-weight:700;font-family:monospace;color:#7dd3fc;letter-spacing:.02em}
.lw-dot{width:8px;height:8px;border-radius:50%;background:#16a34a;display:inline-block;margin-right:6px;vertical-align:middle;animation:pulse 1.5s ease-in-out infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
</style>
</head>
<body>
<div class="card">
  <h1>⚖ Transfer Scale</h1>
  <div class="sub">Configure your scale device</div>

  <div class="tabs">
    <div class="tab active" onclick="switchTab('config')">⚙ Config</div>
    <div class="tab" onclick="switchTab('cal')">📐 Calibration</div>
  </div>

  <!-- ═══════════════════════════════════ CONFIG TAB ══ -->
  <div id="pane-config" class="pane active">
    <div id="cfg-msg" class="flash"></div>

    <div class="section">WiFi Network</div>
    <div class="group">
      <label>SSID</label>
      <input id="ssid" type="text" placeholder="Your 2.4 GHz WiFi name" value="__SSID__" autocomplete="off">
    </div>
    <div class="group">
      <label>Password</label>
      <input id="pass" type="password" placeholder="WiFi password" autocomplete="new-password">
      <div class="hint">Leave blank to keep existing password</div>
    </div>

    <hr>
    <div class="section">open-plaato-server</div>
    <div class="group">
      <label>Base URL</label>
      <input id="url" type="text" placeholder="http://192.168.1.100:3000" value="__URL__">
      <div class="hint">No trailing slash</div>
    </div>
    <div class="group">
      <label>Scale ID</label>
      <input id="sid" type="text" placeholder="scale1" value="__SID__">
      <div class="hint">Used in /api/transfer-scales/:id</div>
    </div>

    <hr>
    <div class="section">Scale Defaults</div>
    <div class="group">
      <label>Calibration Factor</label>
      <input id="cal" type="number" step="0.1" placeholder="-430.0" value="__CAL__">
      <div class="hint">Use the Calibration tab to find this automatically</div>
    </div>
    <div class="row">
      <div class="group">
        <label>Empty Keg (kg)</label>
        <input id="emp" type="number" step="0.001" placeholder="4.028" value="__EMP__">
      </div>
      <div class="group">
        <label>Target Full (kg)</label>
        <input id="tgt" type="number" step="0.001" placeholder="19.15" value="__TGT__">
      </div>
    </div>

    <button id="save-btn" onclick="saveConfig()">Save &amp; Reboot</button>
  </div>

  <!-- ═══════════════════════════════ CALIBRATION TAB ══ -->
  <div id="pane-cal" class="pane">
    <div id="cal-msg" class="flash"></div>

    <div class="live-weight">
      <div>
        <div class="lw-label"><span class="lw-dot" id="lw-dot"></span>Live Reading</div>
        <div class="lw-value" id="lw-val">—</div>
        <div style="font-size:.65rem;color:#475569;margin-top:2px">raw: <span id="lw-raw">—</span> &nbsp; factor: <span id="lw-factor">—</span></div>
      </div>
      <div style="font-size:.75rem;color:#475569;text-align:right">kg<br><span id="lw-status" style="font-size:.65rem">waiting…</span></div>
    </div>

    <div class="progress-steps">
      <div class="ps active" id="ps1"></div>
      <div class="ps" id="ps2"></div>
      <div class="ps" id="ps3"></div>
    </div>

    <!-- Step 1: Tare -->
    <div class="step active" id="step1">
      <div class="step-header">
        <div class="step-num">1</div>
        <div class="step-title">Empty the platform</div>
      </div>
      <div class="step-body">
        Make sure <strong>nothing is sitting on the scale platform</strong>, then tap Tare to zero it out.
      </div>
      <button id="tare-btn" onclick="doTare()">Zero / Tare Scale</button>
    </div>

    <!-- Step 2: Measure -->
    <div class="step" id="step2">
      <div class="step-header">
        <div class="step-num">2</div>
        <div class="step-title">Place known weight</div>
      </div>
      <div class="step-body">
        Place a <strong>known weight</strong> on the platform and enter its exact value below.
        A heavier weight (e.g. a full keg or a 10–20 kg dumbbell) gives better accuracy.
      </div>
      <div class="group">
        <label>Known Weight (kg)</label>
        <input id="known-kg" type="number" step="0.01" placeholder="10.00" value="10.00">
      </div>
      <button id="measure-btn" onclick="doMeasure()">Measure &amp; Calculate Factor</button>
      <button class="secondary" style="margin-top:8px" onclick="goStep(1)">← Back</button>
    </div>

    <!-- Step 3: Result -->
    <div class="step" id="step3">
      <div class="step-header">
        <div class="step-num">3</div>
        <div class="step-title">Review &amp; apply</div>
      </div>
      <div class="step-body">Calibration complete. Review the result and apply it.</div>
      <div class="result-box">
        <div class="result-row"><span class="label">Raw HX711 reading</span><span class="value" id="res-raw">—</span></div>
        <div class="result-row"><span class="label">Calibration factor</span><span class="value" id="res-factor">—</span></div>
        <div class="result-row"><span class="label">Reads back as</span><span class="value" id="res-read">—</span></div>
        <div class="result-row"><span class="label">Error</span><span class="value" id="res-err">—</span></div>
      </div>
      <button class="success" id="apply-btn" onclick="doApply()">✓ Apply &amp; Save to Device</button>
      <button class="secondary" style="margin-top:8px" onclick="goStep(2)">← Redo measurement</button>
      <div id="apply-msg" class="flash" style="margin-top:12px"></div>
    </div>
  </div>
</div>

<script>
var pendingFactor = null;

// ── Tabs ──────────────────────────────────────
function switchTab(name){
  document.querySelectorAll('.tab').forEach(function(t,i){
    t.classList.toggle('active', ['config','cal'][i]===name);
  });
  document.querySelectorAll('.pane').forEach(function(p){
    p.classList.toggle('active', p.id==='pane-'+name);
  });
  if(name==='cal'){startLiveWeight();}else{stopLiveWeight();}
}

// ── Live weight polling ───────────────────────
var lwTimer=null, lwActive=false;
function startLiveWeight(){
  if(lwActive)return;
  lwActive=true;
  pollWeight();
}
function stopLiveWeight(){
  lwActive=false;
  if(lwTimer){clearTimeout(lwTimer);lwTimer=null;}
}
function pollWeight(){
  if(!lwActive)return;
  fetch('/cal/weight')
    .then(function(r){return r.json();})
    .then(function(j){
      if(j.ok){
        document.getElementById('lw-val').textContent=j.kg.toFixed(3);
        document.getElementById('lw-raw').textContent=j.raw;
        document.getElementById('lw-factor').textContent=j.factor.toFixed(2);
        document.getElementById('lw-status').textContent='live';
        document.getElementById('lw-dot').style.background='#16a34a';
      }else{
        document.getElementById('lw-status').textContent='err';
        document.getElementById('lw-dot').style.background='#dc2626';
      }
    })
    .catch(function(){
      document.getElementById('lw-status').textContent='error';
      document.getElementById('lw-dot').style.background='#dc2626';
    })
    .finally(function(){
      if(lwActive)lwTimer=setTimeout(pollWeight,1000);
    });
}

// ── Calibration wizard helpers ────────────────
function goStep(n){
  document.querySelectorAll('.step').forEach(function(s,i){
    s.classList.toggle('active', i+1===n);
  });
  ['ps1','ps2','ps3'].forEach(function(id,i){
    var el=document.getElementById(id);
    el.className='ps'+(i+1<n?' done':i+1===n?' active':'');
  });
}

function calMsg(t,cls){
  var el=document.getElementById('cal-msg');
  el.textContent=t; el.className='flash '+(cls||'info');
  if(cls==='ok'||cls==='err') setTimeout(function(){el.style.display='none';},4000);
}

function setBusy(btnId, busy, label){
  var b=document.getElementById(btnId);
  b.disabled=busy;
  b.innerHTML=busy?'<span class="spinner"></span>Working…':label;
}

// ── Step 1: Tare ──────────────────────────────
function doTare(){
  setBusy('tare-btn', true, 'Zero / Tare Scale');
  fetch('/cal/tare',{method:'POST'})
    .then(function(r){return r.json();})
    .then(function(j){
      setBusy('tare-btn', false, 'Zero / Tare Scale');
      if(j.ok){
        calMsg('','');
        document.getElementById('cal-msg').style.display='none';
        goStep(2);
      } else {
        calMsg('Tare failed: '+(j.msg||'unknown error'),'err');
      }
    })
    .catch(function(e){ setBusy('tare-btn',false,'Zero / Tare Scale'); calMsg('Network error: '+e,'err'); });
}

// ── Step 2: Measure ───────────────────────────
function doMeasure(){
  var kg=parseFloat(document.getElementById('known-kg').value);
  if(isNaN(kg)||kg<=0){ calMsg('Enter a valid weight greater than 0','err'); return; }
  setBusy('measure-btn', true, 'Measure & Calculate Factor');
  fetch('/cal/measure',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({knownKg:kg})
  })
  .then(function(r){return r.json();})
  .then(function(j){
    setBusy('measure-btn',false,'Measure & Calculate Factor');
    if(j.ok){
      pendingFactor=j.factor;
      document.getElementById('res-raw').textContent=j.raw;
      document.getElementById('res-factor').textContent=j.factor.toFixed(2);
      var err=Math.abs(j.readKg-kg);
      var errPct=(err/kg*100).toFixed(2);
      document.getElementById('res-read').textContent=j.readKg.toFixed(3)+' kg';
      var errEl=document.getElementById('res-err');
      errEl.textContent=err.toFixed(3)+' kg ('+errPct+'%)';
      errEl.className='value '+(err<0.05?'good':'warn');
      goStep(3);
    } else {
      calMsg('Measurement failed: '+(j.msg||'unknown error'),'err');
    }
  })
  .catch(function(e){ setBusy('measure-btn',false,'Measure & Calculate Factor'); calMsg('Network error: '+e,'err'); });
}

// ── Step 3: Apply ─────────────────────────────
function doApply(){
  if(pendingFactor===null){ return; }
  setBusy('apply-btn',true,'✓ Apply & Save to Device');
  fetch('/cal/apply',{
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({factor:pendingFactor})
  })
  .then(function(r){return r.json();})
  .then(function(j){
    setBusy('apply-btn',false,'✓ Apply & Save to Device');
    var msg=document.getElementById('apply-msg');
    if(j.ok){
      msg.textContent='✓ Factor '+pendingFactor.toFixed(2)+' saved! You can now update Config → Calibration Factor field and Save & Reboot.';
      msg.className='flash ok';
      // Mirror the value into the config tab input
      document.getElementById('cal').value=pendingFactor.toFixed(2);
    } else {
      msg.textContent='Apply failed: '+(j.msg||'unknown');
      msg.className='flash err';
    }
  })
  .catch(function(e){
    setBusy('apply-btn',false,'✓ Apply & Save to Device');
    var msg=document.getElementById('apply-msg');
    msg.textContent='Network error: '+e; msg.className='flash err';
  });
}

// ── Config save ───────────────────────────────
function saveConfig(){
  var ssid=document.getElementById('ssid').value.trim();
  var pass=document.getElementById('pass').value;
  var url=document.getElementById('url').value.trim();
  var sid=document.getElementById('sid').value.trim();
  var cal=parseFloat(document.getElementById('cal').value);
  var emp=parseFloat(document.getElementById('emp').value);
  var tgt=parseFloat(document.getElementById('tgt').value);
  if(!ssid||!url||!sid){showCfgMsg('SSID, URL and Scale ID are required.','err');return;}
  if(isNaN(cal)){showCfgMsg('Calibration factor must be a number.','err');return;}
  document.getElementById('save-btn').disabled=true;
  document.getElementById('save-btn').textContent='Saving…';
  fetch('/save',{
    method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({ssid:ssid,pass:pass,url:url,sid:sid,cal:cal,emp:emp,tgt:tgt})
  }).then(function(r){return r.json();})
    .then(function(j){
      if(j.ok){showCfgMsg('Saved! Device is rebooting — reconnect to your network shortly.','ok');}
      else{showCfgMsg('Error: '+j.msg,'err');resetSaveBtn();}
    }).catch(function(e){showCfgMsg('Network error: '+e,'err');resetSaveBtn();});
}
function resetSaveBtn(){
  var b=document.getElementById('save-btn');b.disabled=false;b.textContent='Save & Reboot';
}
function showCfgMsg(t,cls){
  var el=document.getElementById('cfg-msg');el.textContent=t;el.className='flash '+cls;
}
</script>
</body>
</html>
)rawhtml";

// ─────────────────────────────────────────────
//  PORTAL — LIGHTWEIGHT JSON EXTRACTOR
// ─────────────────────────────────────────────
String jsonGet(const String& body, const char* key) {
    String search = "\""; search += key; search += "\"";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    idx = body.indexOf(':', idx) + 1;
    while (idx < (int)body.length() && (body[idx]==' '||body[idx]=='\t')) idx++;
    if (body[idx] == '"') {
        int s = idx + 1, e = body.indexOf('"', s);
        return (e < 0) ? "" : body.substring(s, e);
    }
    int s = idx, e = s;
    while (e < (int)body.length() && body[e]!=',' && body[e]!='}') e++;
    String v = body.substring(s, e); v.trim(); return v;
}

// ─────────────────────────────────────────────
//  PORTAL — HTML BUILDER
// ─────────────────────────────────────────────
String buildPortalHTML() {
    String html = FPSTR(PORTAL_HTML);
    html.replace("__SSID__", String(cfg.wifiSSID));
    html.replace("__URL__",  String(cfg.serverBaseURL));
    html.replace("__SID__",  String(cfg.scaleID));
    char buf[16];
    dtostrf(cfg.calibrationFactor, 0, 2, buf); html.replace("__CAL__", buf);
    dtostrf(cfg.defaultEmptyKg,    0, 2, buf); html.replace("__EMP__", buf);
    dtostrf(cfg.defaultTargetKg,   0, 2, buf); html.replace("__TGT__", buf);
    return html;
}

// ─────────────────────────────────────────────
//  PORTAL — HTTP HANDLERS
// ─────────────────────────────────────────────
void handleRoot()    { server.send(200, "text/html", buildPortalHTML()); }
void handleCaptive() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

void handleSave() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"No body\"}"));
        return;
    }
    String body = server.arg("plain");
    String ssid = jsonGet(body, "ssid");
    String pass = jsonGet(body, "pass");
    String url  = jsonGet(body, "url");
    String sid  = jsonGet(body, "sid");
    String cal  = jsonGet(body, "cal");
    String emp  = jsonGet(body, "emp");
    String tgt  = jsonGet(body, "tgt");

    if (ssid.isEmpty() || url.isEmpty() || sid.isEmpty()) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"Missing required fields\"}"));
        return;
    }
    strncpy(cfg.wifiSSID,      ssid.c_str(), sizeof(cfg.wifiSSID)      - 1);
    strncpy(cfg.serverBaseURL, url.c_str(),  sizeof(cfg.serverBaseURL)  - 1);
    strncpy(cfg.scaleID,       sid.c_str(),  sizeof(cfg.scaleID)        - 1);
    if (!pass.isEmpty()) strncpy(cfg.wifiPassword, pass.c_str(), sizeof(cfg.wifiPassword) - 1);
    if (!cal.isEmpty())  cfg.calibrationFactor = cal.toFloat();
    if (!emp.isEmpty())  cfg.defaultEmptyKg    = emp.toFloat();
    if (!tgt.isEmpty())  cfg.defaultTargetKg   = tgt.toFloat();

    saveConfig();
    server.send(200, "application/json", F("{\"ok\":true}"));

    // Register the scale on the server before rebooting.
    // Connects to the configured WiFi first since we're currently in AP mode.
    showMessage("Registering...", sid.c_str(), url.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPassword);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) { delay(200); yield(); }
    if (WiFi.status() == WL_CONNECTED) {
        String regUrl = url + "/api/transfer-scales/" + sid + "/config";
        float regEmp = emp.isEmpty() ? cfg.defaultEmptyKg : emp.toFloat();
        float regTgt = tgt.isEmpty() ? cfg.defaultTargetKg : tgt.toFloat();
        char regBody[128];
        snprintf(regBody, sizeof(regBody),
            "{\"label\":\"%s\",\"empty_keg_weight\":%.3f,\"target_weight\":%.3f}",
            sid.c_str(), regEmp, regTgt);
        Serial.printf("[REG] %s body=%s\n", regUrl.c_str(), regBody);
        WiFiClient regClient;
        HTTPClient regHttp;
        regHttp.begin(regClient, regUrl);
        regHttp.addHeader("Content-Type", "application/json");
        int code = regHttp.POST(regBody);
        regHttp.end();
        Serial.printf("[REG] -> %d\n", code);
        if (code == 200 || code == 201) {
            showMessage("Registered!", sid.c_str(), "Rebooting...");
        } else {
            showMessage("Reg failed", String(code).c_str(), "Rebooting anyway...");
        }
    } else {
        Serial.println("[REG] WiFi connect failed, skipping registration");
        showMessage("Config saved!", "Rebooting...");
    }

    delay(1500);
    ESP.restart();
}

// ── /cal/tare ─────────────────────────────────
void handleCalTare() {
    if (!scale.is_ready()) {
        server.send(503, "application/json", F("{\"ok\":false,\"msg\":\"HX711 not ready\"}"));
        return;
    }
    showMessage("Calibrating...", "Step 1: Taring");
    scale.set_scale();                       // clear factor so tare captures raw ADC offset
    ESP.wdtFeed(); yield();
    scale.tare(CAL_SAMPLES);                 // OFFSET = raw ADC at empty
    scale.set_scale(cfg.calibrationFactor);  // restore factor so live widget shows kg
    calTareDone = true;
    calPendingFactor = 0.0f;
    Serial.println("[CAL] tare done");
    server.send(200, "application/json", F("{\"ok\":true}"));
}

// ── /cal/measure ──────────────────────────────
void handleCalMeasure() {
    if (!calTareDone) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"Tare first\"}"));
        return;
    }
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"No body\"}"));
        return;
    }
    String body = server.arg("plain");
    String kgStr = jsonGet(body, "knownKg");
    if (kgStr.isEmpty()) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"Missing knownKg\"}"));
        return;
    }
    float knownKg = kgStr.toFloat();
    if (knownKg <= 0.f) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"knownKg must be > 0\"}"));
        return;
    }

    showMessage("Calibrating...", "Step 2: Measuring", "Keep still...");
    // scale.set_scale() was called in tare — so get_value() gives raw ADC units
    // Feed watchdog before each blocking read (40 HX711 reads @ 10Hz = ~4s > WDT limit)
    ESP.wdtFeed(); yield();
    long rawReading = scale.get_value(CAL_SAMPLES);
    float factor    = (float)rawReading / knownKg;

    // Verify: apply factor and read back
    scale.set_scale(factor);
    ESP.wdtFeed(); yield();
    float readBack = scale.get_units(CAL_SAMPLES);

    Serial.printf("[CAL] raw=%ld  factor=%.2f  readBack=%.3f kg\n", rawReading, factor, readBack);
    Serial.printf("[CAL] setting calPendingFactor=%.2f\n", factor);

    calPendingFactor = factor;

    char resp[120];
    snprintf(resp, sizeof(resp),
        "{\"ok\":true,\"raw\":%ld,\"factor\":%.4f,\"readKg\":%.4f}",
        rawReading, factor, readBack);
    server.send(200, "application/json", resp);
    showMessage("Cal: measured", "Check browser");
}

// ── /cal/apply ────────────────────────────────
void handleCalApply() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"No body\"}"));
        return;
    }
    String body   = server.arg("plain");
    String facStr = jsonGet(body, "factor");
    if (facStr.isEmpty()) {
        server.send(400, "application/json", F("{\"ok\":false,\"msg\":\"Missing factor\"}"));
        return;
    }
    float factor = facStr.toFloat();
    cfg.calibrationFactor = factor;
    scale.set_scale(factor);
    saveConfig();
    calTareDone = false;
    calPendingFactor = 0.0f;   // reset wizard state

    Serial.printf("[CAL] factor applied: %.2f and saved\n", factor);
    showMessage("Cal factor saved!", String(factor, 2).c_str());

    server.send(200, "application/json", F("{\"ok\":true}"));
}

// ── /cal/weight ───────────────────────────────
void handleCalWeight() {
    if (!scale.is_ready()) {
        server.send(503, "application/json", F("{\"ok\":false,\"msg\":\"HX711 not ready\"}"));
        return;
    }
    // get_value() returns (read_average - OFFSET) in raw ADC counts, independent
    // of the current SCALE setting — safe to call regardless of cal wizard state.
    // Use explicit factor tracking — don't rely on the chip's SCALE register
    // which can be overwritten by concurrent wizard steps.
    float activeFactor = (calPendingFactor != 0.0f) ? calPendingFactor : cfg.calibrationFactor;
    double rawDelta = scale.get_value(5);
    float kg = (activeFactor != 0.0f) ? (float)(rawDelta / (double)activeFactor) : 0.0f;

    Serial.printf("[WEIGHT] raw=%.0f  factor=%.2f  kg=%.3f  pending=%.2f  cfg=%.2f\n",
        rawDelta, activeFactor, kg, calPendingFactor, cfg.calibrationFactor);

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"ok\":true,\"kg\":%.3f,\"raw\":%.0f,\"factor\":%.2f}",
        kg, rawDelta, activeFactor);
    server.send(200, "application/json", resp);
}

// ─────────────────────────────────────────────
//  SETUP PORTAL  (blocks until restart)
// ─────────────────────────────────────────────
void runSetupPortal() {
    Serial.println("[SETUP] Starting AP config portal");

    // Ensure scale is initialised so calibration endpoints work
    if (!scaleReady) {
        scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
        unsigned long t = millis();
        while (!scale.is_ready() && millis() - t < 4000) { delay(100); yield(); }
        if (scale.is_ready()) {
            scale.set_scale(cfg.calibrationFactor);
            scale.tare();
            scaleReady = true;
            Serial.println("[SETUP] Scale initialised for calibration");
        } else {
            Serial.println("[SETUP] HX711 not found — calibration tab will error");
        }
    }

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[SETUP] AP: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    dnsServer.start(DNS_PORT, "*", AP_IP);

    server.on("/",            HTTP_GET,  handleRoot);
    server.on("/save",        HTTP_POST, handleSave);
    server.on("/cal/tare",    HTTP_POST, handleCalTare);
    server.on("/cal/measure", HTTP_POST, handleCalMeasure);
    server.on("/cal/apply",   HTTP_POST, handleCalApply);
    server.on("/cal/weight",  HTTP_GET,  handleCalWeight);
    // Satisfy Windows/Android captive portal detection so they don't pop up a mini browser
    server.on("/connecttest.txt",  HTTP_GET, [](){ server.send(200, "text/plain", "Microsoft Connect Test"); });
    server.on("/ncsi.txt",         HTTP_GET, [](){ server.send(200, "text/plain", "Microsoft NCSI"); });
    server.on("/generate_204",     HTTP_GET, [](){ server.send(204, "text/plain", ""); });  // Android
    server.onNotFound(handleCaptive);
    server.begin();

    renderSetupScreen();

    while (true) {
        dnsServer.processNextRequest();
        server.handleClient();
        yield();
    }
}

// ─────────────────────────────────────────────
//  WIFI STA
// ─────────────────────────────────────────────
void connectWiFi() {
    Serial.printf("[WiFi] → %s\n", cfg.wifiSSID);
    showMessage("Connecting WiFi...", cfg.wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifiSSID, cfg.wifiPassword);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
        delay(300); Serial.print('.');
    }
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (wifiConnected) {
        Serial.printf("\n[WiFi] %s\n", WiFi.localIP().toString().c_str());
        showMessage("WiFi connected!", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] FAILED");
        showMessage("WiFi FAILED", "Offline mode", "Serial 'x' to setup");
    }
    delay(1200);
}

// ─────────────────────────────────────────────
//  HTTP TO PLAATO SERVER
// ─────────────────────────────────────────────
void postWeight() {
    if (!wifiConnected) return;
    String url = String(cfg.serverBaseURL) + "/api/transfer-scales/" + cfg.scaleID + "/data";
    char body[64];
    snprintf(body, sizeof(body), "{\"raw_weight\":%.3f}", currentWeightKg);
    http.begin(wifiClient, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code > 0) Serial.printf("[POST] %d\n", code);
    else {
        Serial.printf("[POST] ERR: %s\n", http.errorToString(code).c_str());
        if (WiFi.status() != WL_CONNECTED) wifiConnected = false;
    }
    http.end();
}

void getRemoteConfig() {
    if (!wifiConnected) return;
    String url = String(cfg.serverBaseURL) + "/api/transfer-scales/" + cfg.scaleID;
    http.begin(wifiClient, url);
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        String pl = http.getString();
        auto ef = [&](const char* k) -> float {
            int i = pl.indexOf(k); if (i<0) return -1.f;
            i = pl.indexOf(':', i) + 1;
            while (i<(int)pl.length()&&(pl[i]==' '||pl[i]=='"')) i++;
            return pl.substring(i).toFloat();
        };
        float ew = ef("empty_keg_weight"), tw = ef("target_weight");
        // Server returns grams when values > 100, kg when <= 100 — normalise to kg
        if (ew > 0.f) { emptyKegKg = (ew > 100.f) ? ew / 1000.f : ew; Serial.printf("[CFG] empty=%.3f kg\n", emptyKegKg); }
        if (tw > 0.f) { targetKg   = (tw > 100.f) ? tw / 1000.f : tw; Serial.printf("[CFG] target=%.3f kg\n", targetKg); }
    } else Serial.printf("[GET] %d\n", code);
    http.end();
}

// ─────────────────────────────────────────────
//  SCALE (normal mode)
// ─────────────────────────────────────────────
void initScale() {
    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
    showMessage("Waiting for scale...");
    unsigned long t = millis();
    while (!scale.is_ready() && millis() - t < 5000) delay(100);
    if (!scale.is_ready()) {
        Serial.println("[SCALE] HX711 not found!");
        showMessage("HX711 ERROR", "Check wiring");
        scaleReady = false;
        return;
    }
    scale.set_scale(cfg.calibrationFactor);
    scale.tare();
    scaleReady = true;
    Serial.printf("[SCALE] ready, cal=%.2f\n", cfg.calibrationFactor);
    showMessage("Scale ready", "Tared at zero");
    delay(1000);
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Transfer Keg Firmware ===");

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    delay(100);  // allow OLED VCC to stabilise before init
    // Scan I2C bus and report found addresses to help diagnose OLED issues
    Serial.print("[I2C] scanning... ");
    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("found 0x%02X ", addr);
        }
    }
    Serial.println();
    if (!display.begin(OLED_ADDRESS, true)) {
        Serial.println("[OLED] init failed — check wiring and address (0x3C or 0x3D)");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.setCursor(18, 24); display.println(F("TRANSFER SCALE"));
        display.setCursor(30, 38); display.println(F("Booting..."));
        display.display();
        delay(600);
    }

    pinMode(SETUP_BTN_PIN, INPUT_PULLUP);
    pinMode(TARE_BTN_PIN,  INPUT_PULLUP);

    bool hasConfig = loadConfig();
    if (!hasConfig) setDefaultConfig();

    // Button-hold check
    bool btnHeld = false;
    if (digitalRead(SETUP_BTN_PIN) == LOW) {
        showMessage("Hold for setup...", "Release to cancel", "3 seconds...");
        unsigned long hold = millis();
        while (digitalRead(SETUP_BTN_PIN) == LOW && millis() - hold < 3000) {
            delay(50); yield();
        }
        btnHeld = (millis() - hold >= 3000);
    }

    if (!hasConfig || btnHeld) {
        runSetupPortal();   // never returns
    }

    emptyKegKg = cfg.defaultEmptyKg;
    targetKg   = cfg.defaultTargetKg;

    connectWiFi();
    initScale();
    if (wifiConnected) getRemoteConfig();

    lastWeighTime = lastPostTime = lastGetTime = millis();
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // WiFi watchdog
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiConnected) { wifiConnected = false; Serial.println("[WiFi] lost"); }
        static unsigned long lastRC = 0;
        if (now - lastRC > 30000) { lastRC = now; WiFi.reconnect(); }
    } else { wifiConnected = true; }

    // Weigh
    if (now - lastWeighTime >= WEIGH_INTERVAL_MS) {
        lastWeighTime = now;
        if (scaleReady && scale.is_ready()) {
            currentWeightKg = max(0.0f, scale.get_units(SCALE_SAMPLES));
            Serial.printf("[SCALE] %.3f kg\n", currentWeightKg);
        }
        renderDisplay();
    }

    if (now - lastPostTime >= POST_INTERVAL_MS) { lastPostTime = now; postWeight(); }
    if (now - lastGetTime  >= GET_INTERVAL_MS)  { lastGetTime  = now; getRemoteConfig(); }

    // Tare button (D7, active LOW, debounced)
    static bool lastTareBtn = HIGH;
    static unsigned long tarePressTime = 0;
    bool tareBtn = digitalRead(TARE_BTN_PIN);
    if (tareBtn == LOW && lastTareBtn == HIGH) {
        tarePressTime = now;  // button just pressed
    }
    if (tareBtn == LOW && (now - tarePressTime >= 50) && lastTareBtn == LOW) {
        // held for 50ms — confirmed press, tare once
        if (scaleReady) {
            scale.tare(5);
            currentWeightKg = 0.0f;
            Serial.println("[TARE] button tare");
            showMessage("Tared!", "Weight zeroed");
            delay(600);
        }
        tarePressTime = now + 60000;  // block repeat until released and pressed again
    }
    lastTareBtn = tareBtn;

    // Serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        switch (cmd) {
            case 't': scale.tare(); Serial.println("[CMD] tared"); showMessage("Tared!","Weight zeroed"); delay(600); break;
            case '+': cfg.calibrationFactor+=10.f; scale.set_scale(cfg.calibrationFactor); Serial.printf("[CMD] cal=%.1f\n",cfg.calibrationFactor); break;
            case '-': cfg.calibrationFactor-=10.f; scale.set_scale(cfg.calibrationFactor); Serial.printf("[CMD] cal=%.1f\n",cfg.calibrationFactor); break;
            case 'r': if(scale.is_ready()) Serial.printf("[RAW] %ld\n",scale.read_average(10)); break;
            case 'c': Serial.printf("[CAL] %.2f\n",cfg.calibrationFactor); break;
            case 's': Serial.printf("[CFG] ssid=%s url=%s id=%s cal=%.2f emp=%.2f tgt=%.2f\n",cfg.wifiSSID,cfg.serverBaseURL,cfg.scaleID,cfg.calibrationFactor,cfg.defaultEmptyKg,cfg.defaultTargetKg); break;
            case 'w': saveConfig(); Serial.println("[CMD] config saved"); break;
            case 'x': Serial.println("[CMD] → setup"); showMessage("Entering setup...",AP_SSID,"pw: scaleme"); delay(1000); runSetupPortal(); break;
            case '?': Serial.println("t=tare +/-=cal r=raw c=calFactor s=showCfg w=saveCfg x=setup ?=help"); break;
        }
    }

    yield();
}
