#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

#define RELAY_PIN       D1
#define EEPROM_SIZE     200
#define MAX_LOGS        80
#define WIFI_TIMEOUT_MS 30000  // 30 detik timeout koneksi WiFi

// ── Alamat EEPROM ──────────────────────────────────────────
#define ADDR_TIMEOUT   0    // unsigned long (4 byte)
#define ADDR_MONITOR   10   // bool          (1 byte)
#define ADDR_SSID      20   // char[33]      (32 char + null)
#define ADDR_PASS      54   // char[65]      (64 char + null)
#define ADDR_WIFI_FLAG 120  // magic byte: 0xAB = kredensial tersimpan
// ──────────────────────────────────────────────────────────

ESP8266WebServer server(80);

unsigned long lastHitMillis  = 0;
unsigned long timeoutMinutes = 1;
bool monitoringEnabled       = true;
bool apMode                  = false;   // true = sedang di mode hotspot

String logs[MAX_LOGS];
int    logIndex = 0;

// Kredensial WiFi runtime (bisa berasal dari EEPROM atau default)
char wifiSSID[33]     = "AP RUMAH";
char wifiPassword[65] = "rotibakar";

// ── Static IP (dipakai saat mode normal/STA) ───────────────
IPAddress local_IP(192, 168, 0, 16);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns1(1, 1, 1, 1);
IPAddress dns2(1, 0, 0, 1);

// ── Konfigurasi Hotspot / AP Mode ─────────────────────────
const char* AP_SSID     = "Watchdog-Setup";
const char* AP_PASSWORD = "";             // open / tanpa password
IPAddress   apIP(192, 168, 4, 1);

// ══════════════════════════════════════════════════════════
//  HELPERS
// ══════════════════════════════════════════════════════════

void addLog(String msg) {
  if (logIndex < MAX_LOGS) {
    logs[logIndex++] = msg;
  } else {
    for (int i = 1; i < MAX_LOGS; i++) logs[i - 1] = logs[i];
    logs[MAX_LOGS - 1] = msg;
  }
}

String getTime() {
  unsigned long s = millis() / 1000;
  int h = s / 3600, m = (s / 60) % 60, sec = s % 60;
  char buf[32];
  sprintf(buf, "%02d:%02d:%02d", h, m, sec);
  return String(buf);
}

// ══════════════════════════════════════════════════════════
//  EEPROM
// ══════════════════════════════════════════════════════════

void saveConfig() {
  EEPROM.put(ADDR_TIMEOUT, timeoutMinutes);
  EEPROM.put(ADDR_MONITOR, monitoringEnabled);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(ADDR_TIMEOUT, timeoutMinutes);
  EEPROM.get(ADDR_MONITOR, monitoringEnabled);
  if (timeoutMinutes == 0 || timeoutMinutes > 60) {
    timeoutMinutes    = 1;
    monitoringEnabled = true;
    saveConfig();
  }
}

void saveWiFiCredentials(const String& ssid, const String& pass) {
  for (int i = 0; i < 33; i++)
    EEPROM.write(ADDR_SSID + i, i < (int)ssid.length() ? ssid[i] : 0);
  for (int i = 0; i < 65; i++)
    EEPROM.write(ADDR_PASS + i, i < (int)pass.length() ? pass[i] : 0);
  EEPROM.write(ADDR_WIFI_FLAG, 0xAB);  // tandai bahwa kredensial tersimpan
  EEPROM.commit();
}

void loadWiFiCredentials() {
  if (EEPROM.read(ADDR_WIFI_FLAG) != 0xAB) return;  // pakai nilai default

  for (int i = 0; i < 32; i++) wifiSSID[i]     = EEPROM.read(ADDR_SSID + i);
  for (int i = 0; i < 64; i++) wifiPassword[i] = EEPROM.read(ADDR_PASS + i);
  wifiSSID[32]     = '\0';
  wifiPassword[64] = '\0';

  // Fallback ke default jika SSID kosong / corrupt
  if (strlen(wifiSSID) == 0) {
    strncpy(wifiSSID,     "AP DEPAN",  sizeof(wifiSSID));
    strncpy(wifiPassword, "rotibakar", sizeof(wifiPassword));
  }
}

// ══════════════════════════════════════════════════════════
//  WIFI
// ══════════════════════════════════════════════════════════

bool tryConnectWiFi() {
  Serial.printf("\nMenghubungkan ke SSID: %s\n", wifiSSID);
  WiFi.mode(WIFI_STA);
  WiFi.config(local_IP, gateway, subnet, dns1, dns2);
  WiFi.begin(wifiSSID, wifiPassword);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  return WiFi.status() == WL_CONNECTED;
}

void startAPMode() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.printf("\n[AP] Hotspot aktif: %s  |  IP: %s\n",
                AP_SSID, apIP.toString().c_str());
}

// ══════════════════════════════════════════════════════════
//  HTML PAGES
// ══════════════════════════════════════════════════════════

// ── Halaman AP Mode: konfigurasi WiFi ─────────────────────
String apPage() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Watchdog WiFi Setup</title>
<style>
body{font-family:Arial;background:#0f172a;color:#fff;margin:0;min-height:100vh;
  display:flex;justify-content:center;align-items:center}
.card{background:#1e293b;padding:24px;border-radius:12px;width:300px;
  box-shadow:0 0 20px #000}
h3{margin:0 0 4px}
.sub{color:#94a3b8;font-size:13px;margin:0 0 14px}
input{width:100%;padding:10px;margin-top:10px;border-radius:8px;border:none;
  background:#0f172a;color:#fff;box-sizing:border-box;font-size:14px}
button{width:100%;padding:11px;margin-top:14px;border-radius:8px;border:none;
  background:#22c55e;color:#000;font-weight:bold;cursor:pointer;font-size:14px}
.warn{background:#451a03;border:1px solid #f97316;border-radius:8px;
  padding:10px;font-size:13px;color:#fdba74;margin-bottom:4px;line-height:1.5}
</style>
</head>
<body>
<div class="card">
  <h3>&#9881; Watchdog WiFi Setup</h3>
  <p class="sub">Konfigurasi koneksi WiFi baru</p>
  <div class="warn">
    &#9888; WiFi tidak terdeteksi selama 30 detik.<br>
    Masukkan SSID dan password WiFi yang baru.
  </div>
  <input type="text"     id="ssid" placeholder="WiFi SSID" autocomplete="off">
  <input type="password" id="pass" placeholder="WiFi Password (kosong = open)">
  <button onclick="save()">&#128190; Simpan &amp; Restart</button>
</div>
<script>
function save(){
  var s=document.getElementById('ssid').value.trim();
  var p=document.getElementById('pass').value;
  if(!s){alert('SSID tidak boleh kosong!');return;}
  fetch('/setwifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))
    .then(function(r){return r.text();})
    .then(function(d){alert(d);});
}
</script>
</body>
</html>
)rawliteral";
}

// ── Halaman Normal Mode: dashboard utama ──────────────────
String webPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Server Watchdog</title>
<style>
body{font-family:Arial;background:#0f172a;color:#fff;margin:0;padding:20px;
  min-height:100vh;display:flex;justify-content:center;align-items:flex-start;
  box-sizing:border-box}
.card{background:#1e293b;padding:20px;border-radius:12px;width:320px;
  box-shadow:0 0 15px #000}
select,button{width:100%;padding:10px;margin-top:10px;border-radius:8px;
  border:none;cursor:pointer}
input[type=text],input[type=password]{width:100%;padding:10px;margin-top:10px;
  border-radius:8px;border:none;background:#0f172a;color:#fff;
  box-sizing:border-box;font-size:14px}
button{background:#22c55e;color:#000;font-weight:bold}
.toggle{display:flex;justify-content:space-between;margin-top:10px}
.info{margin-top:10px;font-size:14px;color:#cbd5e1}
.logbox{background:#020617;margin-top:15px;height:160px;overflow-y:auto;
  font-size:12px;padding:8px;border-radius:8px}
.restartBtn{background:#ef4444;color:#fff;width:auto;padding:6px 12px;margin-top:0}
.btn-warn{background:#f59e0b;color:#000}
hr{border:none;border-top:1px solid #334155;margin:16px 0}
.section-title{font-size:12px;color:#64748b;text-transform:uppercase;
  letter-spacing:.5px;margin-bottom:2px}
.badge{display:inline-block;background:#15803d;color:#fff;border-radius:6px;
  font-size:10px;padding:2px 6px;margin-left:6px;vertical-align:middle}
</style>
</head>
<body>
<div class="card">

  <div style="display:flex;justify-content:space-between;align-items:center">
    <h3 style="margin:0">
      Watchdog
      <span class="badge">OTA &#10003;</span>
    </h3>
    <button class="restartBtn" onclick="restartServer()">Restart</button>
  </div>

  <div class="info">Last Hit : <span id="lastHit">-</span></div>

  <label>Timeout (menit)</label>
  <select id="timeout">
    <option value="1">1</option>
    <option value="2">2</option>
    <option value="3">3</option>
    <option value="5">5</option>
    <option value="10">10</option>
  </select>

  <div class="toggle">
    <span>Monitoring</span>
    <input type="checkbox" id="monitor">
  </div>

  <button onclick="save()">&#128190; Simpan</button>

  <div class="logbox" id="logbox"></div>

  <hr>

  <div class="section-title">&#127760; Ganti Konfigurasi WiFi</div>
  <input type="text"     id="newssid" placeholder="SSID baru" autocomplete="off">
  <input type="password" id="newpass" placeholder="Password baru">
  <button class="btn-warn" onclick="saveWifi()">&#128260; Simpan WiFi &amp; Restart</button>

</div>
<script>
document.getElementById('timeout').value   = %TIMEOUT%;
document.getElementById('monitor').checked = %MONITOR%;

function save(){
  var t=document.getElementById('timeout').value;
  var m=document.getElementById('monitor').checked?1:0;
  fetch('/save?timeout='+t+'&monitor='+m)
    .then(function(r){return r.text();})
    .then(function(){alert('Setting berhasil disimpan');});
}

function restartServer(){
  if(!confirm('Restart server sekarang?'))return;
  fetch('/restart').then(function(r){return r.text();}).then(function(d){alert(d);});
}

function saveWifi(){
  var s=document.getElementById('newssid').value.trim();
  var p=document.getElementById('newpass').value;
  if(!s){alert('SSID tidak boleh kosong!');return;}
  if(!confirm('Ganti WiFi ke "'+s+'"?\nWemos akan restart.'))return;
  fetch('/setwifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p))
    .then(function(r){return r.text();})
    .then(function(d){alert(d);});
}

function updateLastHit(){
  fetch('/status').then(function(r){return r.json();}).then(function(d){
    var min=Math.floor(d.last/60),sec=d.last%60;
    document.getElementById('lastHit').innerText=min+' menit '+sec+' detik';
  });
}

function updateLogs(){
  fetch('/logs').then(function(r){return r.json();}).then(function(d){
    var box=document.getElementById('logbox');
    box.innerHTML='';
    d.forEach(function(l){box.innerHTML+=l+'<br>';});
    box.scrollTop=box.scrollHeight;
  });
}

setInterval(updateLastHit,1000);
setInterval(updateLogs,1500);
updateLastHit();
updateLogs();
</script>
</body>
</html>
)rawliteral";

  html.replace("%TIMEOUT%", String(timeoutMinutes));
  html.replace("%MONITOR%", monitoringEnabled ? "true" : "false");
  return html;
}

// ══════════════════════════════════════════════════════════
//  HTTP HANDLERS
// ══════════════════════════════════════════════════════════

void handleRoot()   { server.send(200, "text/html", webPage()); }
void handleAPRoot() { server.send(200, "text/html", apPage());  }

void handleSave() {
  timeoutMinutes    = server.arg("timeout").toInt();
  monitoringEnabled = server.arg("monitor") == "1";
  saveConfig();
  addLog(getTime() + " [CFG] - Konfigurasi disimpan");
  server.send(200, "text/plain", "OK");
}

void handleHit() {
  lastHitMillis = millis();
  addLog(getTime() + " [INFO] - Server melakukan HIT API");
  server.send(200, "text/plain", "HIT OK");
}

void handleRestart() {
  addLog(getTime() + " [MANUAL] - Restart via Web");
  digitalWrite(RELAY_PIN, HIGH);
  delay(3000);
  digitalWrite(RELAY_PIN, LOW);
  server.send(200, "text/plain", "Server direstart");
}

void handleStatus() {
  unsigned long diff = (millis() - lastHitMillis) / 1000;
  server.send(200, "application/json", "{\"last\":" + String(diff) + "}");
}

void handleLogs() {
  String json = "[";
  for (int i = 0; i < logIndex; i++) {
    json += "\"" + logs[i] + "\"";
    if (i < logIndex - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleSetWiFi() {
  String newSSID = server.arg("ssid");
  String newPass = server.arg("pass");

  if (newSSID.length() == 0) {
    server.send(400, "text/plain", "SSID tidak boleh kosong!");
    return;
  }

  saveWiFiCredentials(newSSID, newPass);
  addLog(getTime() + " [WIFI] - Kredensial baru disimpan, restart...");
  server.send(200, "text/plain",
              "WiFi disimpan! Wemos akan restart dalam 2 detik...");
  delay(2000);
  ESP.restart();
}

// ══════════════════════════════════════════════════════════
//  OTA
// ══════════════════════════════════════════════════════════

void setupOTA() {
  ArduinoOTA.setHostname("watchdog-wemos");
  // ArduinoOTA.setPassword("admin123"); // Opsional: set password OTA

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    addLog(getTime() + " [OTA] - Upload " + type + " dimulai");
    Serial.println("[OTA] Start: " + type);
  });

  ArduinoOTA.onEnd([]() {
    addLog(getTime() + " [OTA] - Upload selesai, restart...");
    Serial.println("\n[OTA] Selesai");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String e;
    if      (error == OTA_AUTH_ERROR)    e = "Auth Failed";
    else if (error == OTA_BEGIN_ERROR)   e = "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) e = "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) e = "Receive Failed";
    else if (error == OTA_END_ERROR)     e = "End Failed";
    addLog(getTime() + " [OTA] Error: " + e);
    Serial.println("[OTA] Error: " + e);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] Ready. Hostname: watchdog-wemos");
}

// ══════════════════════════════════════════════════════════
//  SETUP & LOOP
// ══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  loadConfig();
  loadWiFiCredentials();  // load dari EEPROM, fallback ke default jika belum ada

  if (tryConnectWiFi()) {
    // ── Mode Normal: WiFi berhasil ────────────────────────
    Serial.printf("\n[SYS] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    addLog(getTime() + " [SYS] - WiFi OK: " + WiFi.localIP().toString());

    setupOTA();

    server.on("/",        handleRoot);
    server.on("/save",    handleSave);
    server.on("/hit",     handleHit);
    server.on("/status",  handleStatus);
    server.on("/logs",    handleLogs);
    server.on("/restart", handleRestart);
    server.on("/setwifi", handleSetWiFi);  // ganti WiFi dari dashboard

  } else {
    // ── Mode AP: WiFi gagal dalam 30 detik ───────────────
    Serial.println("\n[SYS] WiFi gagal! Beralih ke mode hotspot...");
    addLog(getTime() + " [SYS] - WiFi gagal, mode AP aktif");

    startAPMode();

    server.on("/",        handleAPRoot);
    server.on("/setwifi", handleSetWiFi);  // simpan kredensial baru & restart
  }

  server.begin();
  lastHitMillis = millis();
  addLog(getTime() + " [SYS] - Watchdog started" + (apMode ? " [AP MODE]" : ""));
}

void loop() {
  server.handleClient();

  if (!apMode) {
    ArduinoOTA.handle();  // selalu handle OTA di mode normal

    if (monitoringEnabled) {
      unsigned long now       = millis();
      unsigned long timeoutMs = timeoutMinutes * 60000UL;
      if (now - lastHitMillis > timeoutMs) {
        addLog(getTime() + " [WARN] - Timeout, relay restart");
        digitalWrite(RELAY_PIN, HIGH);
        delay(3000);
        digitalWrite(RELAY_PIN, LOW);
        lastHitMillis = millis();
      }
    }
  }
}
