/*
  esp32_http_server.ino
  ──────────────────────
  Receives text via HTTP POST from the Windows Python script,
  stores a rolling history of 20 messages, and serves a live
  web UI using Server-Sent Events (SSE) for real-time updates.

  Board  : ESP32 Dev Module
  Core   : Arduino ESP32 (install via Boards Manager)
  Libs   : WiFi, WebServer, ArduinoJson  ← install ArduinoJson via Library Manager

  ── Quick start ─────────────────────────────────────────────────
  1. Set WIFI_SSID + WIFI_PASSWORD below.
  2. Flash to ESP32.
  3. Open Serial Monitor at 115200 baud — note the printed IP address.
  4. Put that IP in voice_http.py on your Windows PC.
  5. Run voice_http.py, then open http://<ESP32_IP> in any browser.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ── Config ────────────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const int   MAX_MESSAGES  = 20;
// ─────────────────────────────────────────────────────────────────────────────

WebServer server(80);

String messages[MAX_MESSAGES];
int    msgCount  = 0;
int    msgHead   = 0;
String latestMsg = "Waiting for speech...";

WiFiClient sseClient;
bool       sseActive = false;

// ── Message store ─────────────────────────────────────────────────────────────
void addMessage(const String& text) {
  int slot = msgCount % MAX_MESSAGES;
  messages[slot] = text;
  latestMsg = text;
  if (msgCount >= MAX_MESSAGES) msgHead = (slot + 1) % MAX_MESSAGES;
  msgCount++;
  Serial.println("[MSG] " + text);
}

// ── Build HTML message list (newest first) ────────────────────────────────────
String buildMsgHTML() {
  if (msgCount == 0) return "<p class='empty'>No messages yet.</p>";
  int total = min(msgCount, MAX_MESSAGES);
  int start = (msgCount > MAX_MESSAGES) ? msgHead : 0;
  String out = "";
  for (int i = total - 1; i >= 0; i--) {
    int idx = (start + i) % MAX_MESSAGES;
    bool isLatest = (i == total - 1);
    out += "<div class='msg" + String(isLatest ? " latest" : "") + "'>";
    out += "<span class='num'>#" + String(msgCount - i) + "</span>";
    out += "<span class='txt'>" + messages[idx] + "</span>";
    out += "</div>";
  }
  return out;
}

// ── Escape a string for embedding in a JSON string value ─────────────────────
String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "\\r");
  return s;
}

// ── Route: GET / — main page ──────────────────────────────────────────────────
void handleRoot() {
  String html = R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Voice Transcript</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0f1117;color:#e2e8f0;padding:20px 16px;min-height:100vh}
header{max-width:680px;margin:0 auto 20px;display:flex;align-items:center;gap:10px}
h1{font-size:1.2rem;font-weight:600;color:#f8fafc;flex:1}
.badge{font-size:.7rem;padding:3px 10px;border-radius:99px;background:#1c2b1c;color:#4ade80;border:1px solid #166534}
.dot{width:8px;height:8px;border-radius:50%;background:#22c55e;animation:pulse 1.4s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
#latest-card{max-width:680px;margin:0 auto 16px;background:#1e2330;border-radius:10px;border-left:4px solid #6366f1;padding:14px 18px}
.card-label{font-size:.65rem;text-transform:uppercase;letter-spacing:.1em;color:#4b5563;margin-bottom:5px}
#latest-text{font-size:1.1rem;color:#e2e8f0;min-height:1.4em;word-break:break-word}
#log{max-width:680px;margin:0 auto}
.msg{display:flex;gap:10px;padding:10px 14px;background:#161b28;border:1px solid #1f2937;border-radius:8px;margin-bottom:6px}
.msg.latest{background:#1c2240;border-color:#4338ca}
.num{font-size:.7rem;color:#374151;min-width:28px;padding-top:3px;flex-shrink:0}
.txt{font-size:.9rem;color:#d1d5db;line-height:1.5;word-break:break-word}
.empty{color:#374151;text-align:center;padding:32px;font-size:.85rem}
#status{max-width:680px;margin:0 auto 10px;font-size:.7rem;color:#4b5563}
</style>
</head>
<body>
<header>
  <div class="dot"></div>
  <h1>Voice Transcript</h1>
  <span class="badge">LIVE</span>
</header>
<div id="latest-card">
  <div class="card-label">Latest</div>
  <div id="latest-text">)";
  html += latestMsg;
  html += R"(</div>
</div>
<div id="status">Connecting...</div>
<div id="log">)";
  html += buildMsgHTML();
  html += R"(</div>
<script>
const latestEl=document.getElementById('latest-text');
const logEl=document.getElementById('log');
const statusEl=document.getElementById('status');
function connect(){
  const es=new EventSource('/events');
  es.onopen=()=>{statusEl.textContent='Live \u2022 updates automatically';};
  es.onmessage=(e)=>{
    const d=JSON.parse(e.data);
    if(d.latest)latestEl.textContent=d.latest;
    if(d.html)logEl.innerHTML=d.html;
  };
  es.onerror=()=>{statusEl.textContent='Reconnecting...';es.close();setTimeout(connect,2000);};
}
connect();
</script>
</body></html>)";

  server.send(200, "text/html", html);
}

// ── Route: POST /message — receive text from Python ───────────────────────────
void handleMessage() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc.containsKey("text")) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON or missing 'text' field\"}");
    return;
  }

  String text = doc["text"].as<String>();
  text.trim();
  if (text.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Empty text\"}");
    return;
  }

  addMessage(text);
  pushSSE();
  server.send(200, "application/json", "{\"ok\":true,\"count\":" + String(msgCount) + "}");
}

// ── Route: GET /ping — connectivity check from Python ─────────────────────────
void handlePing() {
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ── Route: GET /api — JSON transcript dump ────────────────────────────────────
void handleAPI() {
  int total = min(msgCount, MAX_MESSAGES);
  int start = (msgCount > MAX_MESSAGES) ? msgHead : 0;
  String json = "{\"count\":" + String(msgCount) + ",\"latest\":\"" + jsonEscape(latestMsg) + "\",\"messages\":[";
  for (int i = total - 1; i >= 0; i--) {
    int idx = (start + i) % MAX_MESSAGES;
    if (i < total - 1) json += ",";
    json += "\"" + jsonEscape(messages[idx]) + "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// ── Route: GET /events — SSE stream ───────────────────────────────────────────
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
}

// ── Push an SSE event to any open browser connection ─────────────────────────
void pushSSE() {
  if (!sseActive || !sseClient.connected()) { sseActive = false; return; }
  String html = buildMsgHTML();
  html.replace("\"", "\\\"");
  html.replace("\n", "");
  String payload = "data: {\"latest\":\"" + jsonEscape(latestMsg) + "\",\"html\":\"" + html + "\"}\n\n";
  sseClient.print(payload);
  sseClient.flush();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.print("\n[WiFi] Connecting to ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/ping",    HTTP_GET,  handlePing);
  server.on("/events",  HTTP_GET,  handleEvents);
  server.on("/api",     HTTP_GET,  handleAPI);
  server.on("/message", HTTP_POST, handleMessage);

  server.begin();
  Serial.println("[HTTP] Server started on port 80");
  Serial.print("[HTTP] Open in browser: http://");
  Serial.println(WiFi.localIP());
  Serial.println("\n>>> Ready — waiting for messages from Windows PC...");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
}
