#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── WiFi credentials ──────────────────────────────────────
const char* SSID     = "YourWiFiName";
const char* PASSWORD = "YourWiFiPassword";

// ── OLED config ───────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS 0x3C

// I2C pins (standard ESP32)
#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

// ── Auto-clear timer ──────────────────────────────────────
unsigned long lastMessageTime = 0;
const unsigned long CLEAR_AFTER_MS = 20000;  // 20 seconds
bool displayActive = false;

// ── Display helper (word-wrap) ────────────────────────────
void showOnOLED(String text) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);   // manual wrapping

  int lineHeight    = 10;   // pixels per line at text size 1
  int maxCharsPerLine = 21; // 128px / ~6px per char
  int maxLines      = 6;    // 64px / 10px per line
  int currentLine   = 0;
  int y             = 0;

  String remaining      = text;
  String currentLineText = "";

  while (remaining.length() > 0 && currentLine < maxLines) {

    // Find next word
    int spaceIndex = remaining.indexOf(' ');
    String word;
    if (spaceIndex == -1) {
      word      = remaining;
      remaining = "";
    } else {
      word      = remaining.substring(0, spaceIndex);
      remaining = remaining.substring(spaceIndex + 1);
    }

    // Force-break any word longer than one full line
    while (word.length() > maxCharsPerLine && currentLine < maxLines) {
      String chunk = word.substring(0, maxCharsPerLine);
      word = word.substring(maxCharsPerLine);

      if (currentLineText.length() > 0) {
        display.setCursor(0, y);
        display.println(currentLineText);
        currentLine++;
        y += lineHeight;
        currentLineText = "";
      }

      display.setCursor(0, y);
      display.println(chunk);
      currentLine++;
      y += lineHeight;
    }

    // Check if adding this word exceeds line width
    String testLine = currentLineText.length() == 0
                      ? word
                      : currentLineText + " " + word;

    if (testLine.length() > maxCharsPerLine) {
      // Print current line, move word to next line
      display.setCursor(0, y);
      display.println(currentLineText);
      currentLine++;
      y += lineHeight;
      currentLineText = word;
    } else {
      currentLineText = testLine;
    }
  }

  // Print any remaining text on the last line
  if (currentLineText.length() > 0 && currentLine < maxLines) {
    display.setCursor(0, y);
    display.println(currentLineText);
  }

  display.display();
  Serial.println("[OLED] " + text);
}

// ── Status screen on boot ─────────────────────────────────
void showStatus(String line1, String line2 = "", String line3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);  display.println(line1);
  display.setCursor(0, 20); display.println(line2);
  display.setCursor(0, 40); display.println(line3);
  display.display();
}

// ── HTTP POST /message handler ────────────────────────────
void handleMessage() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String body = server.arg("plain");
  Serial.println("[HTTP] Received body: " + body);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    Serial.println("[JSON] Parse failed: " + String(error.c_str()));
    server.send(400, "text/plain", "Bad JSON");
    return;
  }

  const char* text = doc["text"];
  if (text == nullptr || strlen(text) == 0) {
    server.send(400, "text/plain", "No text field");
    return;
  }

  showOnOLED(String(text));
  lastMessageTime = millis();   // reset timer on every new message
  displayActive   = true;
  server.send(200, "text/plain", "OK");
}

// ── Root endpoint (health check) ─────────────────────────
void handleRoot() {
  server.send(200, "text/plain", "ESP32 Voice Display Ready");
}

// ── Not found ─────────────────────────────────────────────
void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED not found! Check wiring.");
    while (true);
  }

  display.clearDisplay();
  display.display();
  showStatus("Connecting", "to WiFi...");

  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi FAILED", "Check SSID &", "password");
    Serial.println("\n[ERROR] WiFi connection failed!");
    while (true);
  }

  String ip = WiFi.localIP().toString();
  Serial.println("\n[WiFi] Connected! IP: " + ip);

  showStatus("Connected!", ip, "Port: 80");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/message", HTTP_POST, handleMessage);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] Server started on port 80");

  delay(3000);
  showStatus("Ready!", "Waiting for", "voice input...");
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
  server.handleClient();

  // ── Auto-clear with 5s countdown ──────────────────────
  if (displayActive) {
    unsigned long elapsed = millis() - lastMessageTime;

    if (elapsed >= CLEAR_AFTER_MS) {
      display.clearDisplay();
      display.display();
      displayActive = false;
      Serial.println("[OLED] Cleared after 20s timeout");

    } else if (elapsed >= 15000) {
      // Show countdown in bottom-right corner (last 5 seconds)
      int secondsLeft = (CLEAR_AFTER_MS - elapsed) / 1000 + 1;
      display.fillRect(100, 56, 28, 8, SSD1306_BLACK);  // erase old number
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(100, 56);
      display.print(secondsLeft);
      display.print("s");
      display.display();
    }
  }

  // ── WiFi reconnect ────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected, reconnecting...");
    showStatus("WiFi lost...", "Reconnecting");
    WiFi.reconnect();
    delay(3000);
  }
}