#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6E400001-B5A4-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A4-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A4-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// ── Finger config — edit bentMin/bentMax/neutral to your sensor readings ──
struct FingerConfig {
  int pin;
  String word;
  int bentMin;
  int bentMax;
  int neutral;
  String lastSent;
};

FingerConfig fingers[] = {
  // { pin,  word,                    bentMin, bentMax, neutral, lastSent }
  { 32, "Left Hand Thumb Word",    2800,    3200,    3500,    "" },
  { 33, "Left Hand Index Word",    2800,    3200,    3500,    "" },
  { 34, "Left Hand Middle Word",   2800,    3200,    3500,    "" },
  { 35, "Left Hand Ring Word",     2800,    3200,    3500,    "" },
  { 36, "Left Hand Pinky Word",    2800,    3200,    3500,    "" },
};

const int NUM_FINGERS = 5;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println(">> Client connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println(">> Client disconnected");
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting LEFT HAND ESP32...");

  for (int i = 0; i < NUM_FINGERS; i++) {
    pinMode(fingers[i].pin, INPUT);
  }

  BLEDevice::init("ESP32-LeftHand");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Advertising as 'ESP32-LeftHand'...");
}

void loop() {
  if (deviceConnected) {
    for (int i = 0; i < NUM_FINGERS; i++) {
      int raw = analogRead(fingers[i].pin);
      Serial.print("Finger" + String(i) + ":" + String(raw) + "  ");

      // ── Check if bent ────────────────────────────────────────────────────
      if (raw >= fingers[i].bentMin && raw <= fingers[i].bentMax) {
        if (fingers[i].lastSent != fingers[i].word) {
          pTxCharacteristic->setValue(fingers[i].word.c_str());
          pTxCharacteristic->notify();
          Serial.println("\nSent: " + fingers[i].word);
          fingers[i].lastSent = fingers[i].word;
        }
      }

      // ── Check if neutral/flat — send RESET ───────────────────────────────
      if (raw > fingers[i].neutral) {
        if (fingers[i].lastSent != "") {
          String resetMsg = "RESET_" + String(i);
          pTxCharacteristic->setValue(resetMsg.c_str());
          pTxCharacteristic->notify();
          Serial.println("\nSent: " + resetMsg);
          fingers[i].lastSent = "";
        }
      }
    }
    Serial.println();
    delay(200);
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Restarting advertising...");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
}