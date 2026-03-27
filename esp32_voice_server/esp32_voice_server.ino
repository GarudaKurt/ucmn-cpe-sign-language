/*
  esp32_voice_server.ino
  ──────────────────────
  Receives text over Bluetooth Classic (RFCOMM) from the Python script,
  stores a rolling history of 20 messages, and serves them via a built-in
  Wi-Fi web server.  The web page auto-refreshes every 2 seconds via SSE
  (Server-Sent Events) so you get near-real-time display without polling.

  Board: ESP32 (select "ESP32 Dev Module" in Arduino IDE)
  Libraries needed:
    - BluetoothSerial  (built-in with ESP32 Arduino core)
    - WiFi             (built-in)
    - WebServer        (built-in)

  ── Setup ──────────────────────────────────────────────────────────────────
  1. Set WIFI_SSID and WIFI_PASSWORD below.
  2. Flash to your ESP32.
  3. Open Serial Monitor at 115200 baud.
  4. Pair your PC/Pi to the ESP32 via Bluetooth (name: "ESP32-Voice").
  5. Run voice_bluetooth.py on your PC/Pi.
  6. Visit http://<ESP32_IP> in a browser on the same Wi-Fi network.
*/

#include "BluetoothSerial.h"
#include <WiFi.h>
#include <WebServer.h>

// ── Config ────────────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const int   MAX_MESSAGES  = 20;       // rolling history size
// ─────────────────────────────────────────────────────────────────────────────

BluetoothSerial SerialBT;
WebServer       server(80);

// Rolling message store
String messages[MAX_MESSAGES];
int    msgCount   = 0;   // total received (ever)
int    msgHead    = 0;   // index of oldest slot in circular buffer
String latestMsg  = "";

// SSE client tracking (simple: one client at a time)
WiFiClient sseClient;
bool       sseActive = false;

// ── Helper: add a new message to the circular buffer ─────────────────────────
void addMessage(const String& text) {
  int slot = msgCount % MAX_MESSAGES;
  messages[slot] = text;
  latestMsg = text;
  if (msgCount >= MAX_MESSAGES) msgHead = (slot + 1) % MAX_MESSAGES;
  msgCount++;
  Serial.println("[BT RX] " + text);
}

// ── Helper: build the ordered list HTML (oldest → newest) ────────────────────
String buildMessageListHTML() {
  if (msgCount == 0) return "<p class=\"empty\">No messages yet.</p>";

  int total  = min(msgCount, MAX_MESSAGES);
  int start  = (msgCount > MAX_MESSAGES) ? msgHead : 0;
  String out = "";

  for (int i = total - 1; i >= 0; i--) {           // newest first
    int idx = (start + i) % MAX_MESSAGES;
    out += "<div class=\"msg" + String(i == total - 1 ? " latest" : "") + "\">";
    out += "<span class=\"num\">#" + String(msgCount - i) + "</span>";
    out += "<span class=\"text\">" + messages[idx] + "</span>";
    out += "</div>";
  }
  return out;
}

// ── Web: main page ─────────────────────────────────────────────────────────
void handleRoot() {
  String html = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Voice Transcript</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,sans-serif;background:#0f1117;color:#e2e8f0;min-height:100vh;padding:24px 16px}
  header{max-width:700px;margin:0 auto 24px;display:flex;align-items:center;gap:12px}
  h1{font-size:1.25rem;font-weight:600;color:#f8fafc}
  .dot{width:10px;height:10px;border-radius:50%;background:#22c55e;animation:pulse 1.5s infinite}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
  #latest-box{max-width:700px;margin:0 auto 20px;background:#1e2330;border:1px solid #2d3748;border-left:4px solid #6366f1;border-radius:10px;padding:16px 20px}
  #latest-box .label{font-size:.7rem;text-transform:uppercase;letter-spacing:.08em;color:#64748b;margin-bottom:6px}
  #latest-text{font-size:1.15rem;color:#e2e8f0;min-height:1.4em}
  #log{max-width:700px;margin:0 auto}
  .msg{display:flex;gap:12px;padding:12px 16px;border-radius:8px;margin-bottom:8px;background:#1a1f2e;border:1px solid #252d3d;transition:background .3s}
  .msg.latest{background:#1e2440;border-color:#4f46e5}
  .num{font-size:.75rem;color:#4b5563;min-width:32px;padding-top:2px}
  .text{font-size:.95rem;color:#cbd5e1;line-height:1.5}
  .empty{color:#4b5563;font-size:.9rem;text-align:center;padding:40px}
  #status{max-width:700px;margin:0 auto 12px;font-size:.75rem;color:#4b5563}
</style>
</head>
<body>
<header>
  <div class="dot"></div>
  <h1>Voice Transcript &mdash; Live</h1>
</header>
<div id="latest-box">
  <div class="label">Latest message</div>
  <div id="latest-text">Waiting for speech&hellip;</div>
</div>
<div id="status">Connecting to live feed&hellip;</div>
<div id="log">)rawhtml";

  html += buildMessageListHTML();
  html += R"rawhtml(
</div>
<script>
const latestEl = document.getElementById('latest-text');
const logEl    = document.getElementById('log');
const statusEl = document.getElementById('status');

function connect() {
  const src = new EventSource('/events');
  src.onopen = () => { statusEl.textContent = 'Live \u2022 updates automatically'; };
  src.onmessage = (e) => {
    const d = JSON.parse(e.data);
    latestEl.textContent = d.latest || '\u2014';
    logEl.innerHTML = d.html;
  };
  src.onerror = () => {
    statusEl.textContent = 'Reconnecting\u2026';
    src.close();
    setTimeout(connect, 2000);
  };
}
connect();
</script>
</body>
</html>
)rawhtml";

  server.send(200, "text/html", html);
}

// ── Web: SSE endpoint ─────────────────────────────────────────────────────────
void handleEvents() {
  sseClient = server.client();
  sseActive = true;

  sseClient.println("HTTP/1.1 200 OK");
  sseClient.println("Content-Type: text/event-stream");
  sseClient.println("Cache-Control: no-cache");
  sseClient.println("Connection: keep-alive");
  sseClient.println("Access-Control-Allow-Origin: *");
  sseClient.println();
  sseClient.flush();
  // Client stays open; we push updates from loop()
}

// ── Web: /api/messages (JSON) ─────────────────────────────────────────────────
void handleAPI() {
  String json = "{\"count\":" + String(msgCount) + ",\"latest\":\"" + latestMsg + "\",\"messages\":[";
  int total = min(msgCount, MAX_MESSAGES);
  int start = (msgCount > MAX_MESSAGES) ? msgHead : 0;
  for (int i = total - 1; i >= 0; i--) {
    int idx = (start + i) % MAX_MESSAGES;
    if (i < total - 1) json += ",";
    json += "\"" + messages[idx] + "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// ── SSE push helper ───────────────────────────────────────────────────────────
void pushSSE() {
  if (!sseActive || !sseClient.connected()) {
    sseActive = false;
    return;
  }
  // Escape quotes in the latest message for JSON safety
  String safe = latestMsg;
  safe.replace("\"", "\\\"");

  // Build HTML snippet (escape double-quotes for JSON embedding)
  String htmlSnippet = buildMessageListHTML();
  htmlSnippet.replace("\"", "\\\"");
  htmlSnippet.replace("\n", "");

  String payload = "data: {\"latest\":\"" + safe + "\",\"html\":\"" + htmlSnippet + "\"}\n\n";
  sseClient.print(payload);
  sseClient.flush();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // Bluetooth
  SerialBT.begin("ESP32-Voice");
  Serial.println("[BT] Device ready. Name: ESP32-Voice");

  // Wi-Fi
  Serial.print("[WiFi] Connecting to " + String(WIFI_SSID));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());

  // Routes
  server.on("/",        handleRoot);
  server.on("/events",  handleEvents);
  server.on("/api",     handleAPI);
  server.begin();
  Serial.println("[HTTP] Web server started on port 80");
  Serial.println(">>> Open: http://" + WiFi.localIP().toString());
}

// ── Loop ──────────────────────────────────────────────────────────────────────
String btBuffer = "";

void loop() {
  server.handleClient();

  // Read Bluetooth data (newline-delimited)
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    if (c == '\n') {
      btBuffer.trim();
      if (btBuffer.length() > 0) {
        addMessage(btBuffer);
        pushSSE();
      }
      btBuffer = "";
    } else {
      btBuffer += c;
    }
  }
}
