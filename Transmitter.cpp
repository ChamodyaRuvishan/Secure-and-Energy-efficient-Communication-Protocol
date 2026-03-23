is this reciver or transmitter code #include <esp_now.h>
#include <WiFi.h>
#include <Crypto.h>
#include <SHA256.h>
#include "esp_sleep.h"

// MAC address of the peer (Device B)
uint8_t peerMAC[] = {0xEC, 0xE3, 0x34, 0x22, 0x59, 0x08}; // MAC of Device B

// GPIO Pins
#define RF_WAKEUP_PIN 4
#define SENSOR_WAKE_PIN 15
#define MOISTURE_SENSOR_PIN 33

typedef struct SensorMessage {
  char msg[32];
  byte hash[32];
  uint16_t lsb16;
  int moisture;
} SensorMessage;

SensorMessage outgoingMessage;
SensorMessage incomingMessage;

volatile bool authReceived = false;
bool ackReceived = false;

// ISR for RF wake-up
void IRAM_ATTR handleRFWakeUp() {
  authReceived = true;
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void onDataReceived(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
  if (len == sizeof(incomingMessage)) {
    memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));
    Serial.printf("ACK Received: %s\n", incomingMessage.msg);
    ackReceived = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n===== ESP32 Sensor Node Booting =====");

  // Print wake-up cause
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  if (wakeupReason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Wake-up reason: EXT0 (Sensor Wake Pin)");
  } else if (wakeupReason == ESP_SLEEP_WAKEUP_UNDEFINED) {
    Serial.println("Wake-up reason: Power On Reset");
  } else {
    Serial.printf("Wake-up reason code: %d\n", wakeupReason);
  }

  pinMode(RF_WAKEUP_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RF_WAKEUP_PIN), handleRFWakeUp, FALLING);

  pinMode(SENSOR_WAKE_PIN, INPUT);
  pinMode(MOISTURE_SENSOR_PIN, INPUT);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 1);

  delay(300);

  // Create random token and hash
  uint32_t randomNumber = esp_random();
  byte randomBytes[4] = {
    (uint8_t)(randomNumber >> 24),
    (uint8_t)(randomNumber >> 16),
    (uint8_t)(randomNumber >> 8),
    (uint8_t)randomNumber
  };

  SHA256 sha256;
  sha256.reset();
  sha256.update(randomBytes, sizeof(randomBytes));
  sha256.finalize(outgoingMessage.hash, sizeof(outgoingMessage.hash));

  outgoingMessage.lsb16 = (outgoingMessage.hash[30] << 8) | outgoingMessage.hash[31];
  snprintf(outgoingMessage.msg, sizeof(outgoingMessage.msg), "%04X", outgoingMessage.lsb16);

  // WiFi + ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("Device A MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  // ---------------------------
  // Read Moisture Sensor
  // ---------------------------
  int moistureValue = analogRead(MOISTURE_SENSOR_PIN);

  // Map: 4095 = dry → 0%, 0 = wet → 100%
  int moisturePercent = map(moistureValue, 4095, 0, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  outgoingMessage.moisture = moisturePercent;

  Serial.printf("Sending Data: MoistureRaw=%d  Moisture%%=%d  Token=%s\n",
                moistureValue, moisturePercent, outgoingMessage.msg);

  esp_now_send(peerMAC, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
}

unsigned long timer = 0;

void loop() {
  if (authReceived) {
    Serial.println("RF Auth Signal Detected!");
    authReceived = false;
  }

  if (ackReceived) {
    Serial.println("ACK Confirmed. Communication success.");
    ackReceived = false;
  }

  if (millis() - timer > 4000) {
    Serial.println("Going to Deep Sleep...");
    delay(500);
    esp_deep_sleep_start();
  }
}