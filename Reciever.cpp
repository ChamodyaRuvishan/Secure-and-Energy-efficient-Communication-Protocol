#include <esp_now.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// ===== Blynk Setup =====
#define BLYNK_TEMPLATE_ID "TMPL60g0gRIWP"
#define BLYNK_TEMPLATE_NAME "RF 125 WUR"
#define BLYNK_AUTH_TOKEN "Your_Auth_Token"

char ssid[] = "YourWiFiSSID";
char pass[] = "YourWiFiPassword";

// ===== ESP-NOW Setup =====
uint8_t sensorMAC[] = {0x6C, 0xC8, 0x40, 0x8C, 0xF0, 0xEC}; // MAC of Device A

typedef struct SensorMessage {
  char msg[32];
  byte hash[32];
  uint16_t lsb16;
  int moisture;
} SensorMessage;

SensorMessage incomingMessage;
SensorMessage outgoingMessage;

// ===== Function Declarations =====
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onDataRecv(const esp_now_recv_info* info, const uint8_t* incomingData, int len);

void setup() {
  Serial.begin(115200);

  // ----- ESP-NOW Init -----
  WiFi.mode(WIFI_STA);
  Serial.print("Device B MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, sensorMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // ----- Blynk Init -----
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

// ===== Blynk Switch Callback =====
BLYNK_WRITE(V1) {
  int switchState = param.asInt(); // 0 = OFF, 1 = ON
  Serial.print("Switch State: "); Serial.println(switchState);

  // Send wakeup signal to Device A
  strcpy(outgoingMessage.msg, switchState ? "WAKE UP" : "SLEEP");
  esp_now_send(sensorMAC, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
}

void loop() {
  Blynk.run(); // Keep Blynk connection alive
  // ESP-NOW works via interrupts, no polling needed
}

// ===== ESP-NOW Callbacks =====
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ACK Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void onDataRecv(const esp_now_recv_info* info, const uint8_t* incomingData, int len) {
  memcpy(&incomingMessage, incomingData, sizeof(incomingMessage));

  Serial.println("=== Data Received ===");
  Serial.printf("Token: %s\n", incomingMessage.msg);
  Serial.printf("Moisture: %d\n", incomingMessage.moisture);

  // Send ACK back automatically
  strcpy(outgoingMessage.msg, "ACK Received OK");
  esp_now_send(sensorMAC, (uint8_t*)&outgoingMessage, sizeof(outgoingMessage));
}