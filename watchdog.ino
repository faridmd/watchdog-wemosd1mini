#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define RELAY_PIN D1
#define EEPROM_SIZE 64
#define MAX_LOGS 80

ESP8266WebServer server(80);

unsigned long lastHitMillis = 0;
unsigned long timeoutMinutes = 1;
bool monitoringEnabled = true;

String logs[MAX_LOGS];
int logIndex = 0;

// ===== WIFI CONFIG =====
const char* ssid = "AP DEPAN";
const char* password = "rotibakar";
// =======================

// ===== STATIC IP CONFIG =====
IPAddress local_IP(192,168,5,16);
IPAddress gateway(192,168,5,1);
IPAddress subnet(255,255,255,0);
IPAddress dns1(1,1,1,1);
IPAddress dns2(1,0,0,1);
// ============================

void addLog(String msg) {
  if (logIndex < MAX_LOGS) {
    logs[logIndex++] = msg;
  } else {
    for (int i = 1; i < MAX_LOGS; i++) {
      logs[i - 1] = logs[i];
    }
    logs[MAX_LOGS - 1] = msg;
  }
}

String getTime() {
  unsigned long s = millis() / 1000;
  int h = s / 3600;
  int m = (s / 60) % 60;
  int sec = s % 60;
  char buf[32];
  sprintf(buf, "%02d:%02d:%02d", h, m, sec);
  return String(buf);
}

void saveConfig() {
  EEPROM.put(0, timeoutMinutes);
  EEPROM.put(10, monitoringEnabled);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.get(0, timeoutMinutes);
  EEPROM.get(10, monitoringEnabled);

  if (timeoutMinutes == 0 || timeoutMinutes > 60) {
    timeoutMinutes = 1;
    monitoringEnabled = true;
    saveConfig();
  }
}

String webPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{
  font-family:Arial;
  background:#0f172a;
  color:white;
  margin:0;
  min-height:100vh;
  display:flex;
  justify-content:center;
  align-items:center;
}
.card{background:#1e293b;padding:20px;border-radius:12px;width:320px;box-shadow:0 0 15px #000}
select,button{width:100%;padding:10px;margin-top:10px;border-radius:8px;border:none}
button{background:#22c55e;color:black;font-weight:bold}
.toggle{display:flex;justify-content:space-between;margin-top:10px}
.info{margin-top:10px;font-size:14px;color:#cbd5e1}
.logbox{background:#020617;margin-top:15px;height:160px;overflow-y:auto;font-size:12px;padding:8px;border-radius:8px}
.restartBtn{background:#ef4444;color:white;width:auto;padding:6px 12px;margin-top:0}
</style>
</head>
<body>
<div class="card">

<div style="display:flex;justify-content:space-between;align-items:center">
  <h3>Server Watchdog</h3>
  <button class="restartBtn" onclick="restartServer()">Restart</button>
</div>

<div class="info">
Last Hit : <span id="lastHit">-</span>
</div>

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

<button onclick="save()">Simpan</button>

<div class="logbox" id="logbox"></div>
</div>

<script>
document.getElementById("timeout").value = %TIMEOUT%;
document.getElementById("monitor").checked = %MONITOR%;

function save(){
  let t = document.getElementById("timeout").value;
  let m = document.getElementById("monitor").checked ? 1 : 0;

  fetch(`/save?timeout=${t}&monitor=${m}`)
  .then(r=>r.text())
  .then(d=>alert("Setting berhasil disimpan"));
}

function restartServer(){
  if(!confirm("Restart server sekarang?")) return;

  fetch('/restart')
    .then(r=>r.text())
    .then(d=>alert(d));
}

function updateLastHit(){
  fetch('/status')
    .then(r=>r.json())
    .then(d=>{
      let min = Math.floor(d.last / 60);
      let sec = d.last % 60;
      document.getElementById("lastHit").innerText =
        min + " menit " + sec + " detik";
    });
}

function updateLogs(){
  fetch('/logs')
    .then(r=>r.json())
    .then(d=>{
      let box = document.getElementById("logbox");
      box.innerHTML = "";
      d.forEach(l=>{
        box.innerHTML += l + "<br>";
      });
      box.scrollTop = box.scrollHeight;
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

void handleRoot() {
  server.send(200, "text/html", webPage());
}

void handleSave() {
  timeoutMinutes = server.arg("timeout").toInt();
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
  String json = "{\"last\":" + String(diff) + "}";
  server.send(200, "application/json", json);
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

void setup() {
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  loadConfig();

  if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
  // if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure Static IP");
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/hit", handleHit);
  server.on("/status", handleStatus);
  server.on("/logs", handleLogs);
  server.on("/restart", handleRestart);

  server.begin();

  lastHitMillis = millis();
  addLog(getTime() + " [SYS] - Watchdog started");
}

void loop() {
  server.handleClient();

  if (monitoringEnabled) {
    unsigned long now = millis();
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
