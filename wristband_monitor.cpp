/**
 * Project: ESP32 IoT Health Monitoring System
 * File: wristband_monitor.cpp (Multi-Receiver Unit - Row Status View)
 * Target MCU: ESP32 Dev Module (30-pin)
 * 
 * Hardware Pinout:
 *   - LCD 16x2 I2C SDA -> GPIO 21
 *   - LCD 16x2 I2C SCL -> GPIO 22
 *   - Buzzer Positive  -> GPIO 33
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>

// ==================== CONFIGURATION ====================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_SERVER   = "YOUR_HIVEMQ_HOST.hivemq.cloud";
const int   MQTT_PORT     = 8883;
const char* MQTT_USER     = "YOUR_MQTT_USERNAME";
const char* MQTT_PASS     = "YOUR_MQTT_PASSWORD";

#define BUZZER_PIN 33
#define NUM_DEVICES 5

// ==================== DATA STRUCTURE ====================
enum HealthStatus { STATUS_NORMAL, STATUS_WARNING, STATUS_CRITICAL, STATUS_OFFLINE };

struct DeviceData {
  int bpm = 0;
  int spo2 = 0;
  bool isOnline = false;
  unsigned long lastUpdate = 0;
  HealthStatus status = STATUS_OFFLINE;
};

DeviceData devices[NUM_DEVICES];

// ==================== GLOBAL OBJECTS ====================
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPageSwitch = 0;
unsigned long lastBuzzerToggle = 0;
unsigned long lastMqttRetry = 0;

int currentPage = 0; // Page 0: Devices 1-3 | Page 1: Devices 4-5
bool buzzerState = false;

// ==================== FUNCTION DECLARATIONS ====================
void setupWiFi();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void evaluateDeviceStatus(int id);
void updateDisplayAndAlerts();
void triggerBuzzer(HealthStatus highestStatus);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("IoT Monitor 5Ch");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  espClient.setInsecure();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  setupWiFi();
  lcd.clear();
}

// ==================== MAIN LOOP ====================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }

  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttRetry > 5000) {
      lastMqttRetry = now;
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // Timeout Check: ถ้ารับค่าไม่ได้เกิน 15 วินาที -> OFFLINE
  unsigned long now = millis();
  for (int i = 0; i < NUM_DEVICES; i++) {
    if (devices[i].isOnline && (now - devices[i].lastUpdate > 15000)) {
      devices[i].isOnline = false;
      devices[i].status = STATUS_OFFLINE;
      devices[i].bpm = 0;
      devices[i].spo2 = 0;
    }
  }

  updateDisplayAndAlerts();
}

// ==================== NETWORK FUNCTIONS ====================
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void reconnectMQTT() {
  String clientId = "ESP32-MultiReceiver-";
  clientId += String(random(0xffff), HEX);

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    for (int i = 1; i <= NUM_DEVICES; i++) {
      String bpmTopic = "wristband/" + String(i) + "/bpm";
      String spo2Topic = "wristband/" + String(i) + "/spo2";
      mqttClient.subscribe(bpmTopic.c_str());
      mqttClient.subscribe(spo2Topic.c_str());
    }
  }
}

// ==================== MQTT CALLBACK ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[16];
  if (length >= sizeof(message)) length = sizeof(message) - 1;
  memcpy(message, payload, length);
  message[length] = '\0';

  String topicStr = String(topic);
  int val = atoi(message);

  int firstSlash = topicStr.indexOf('/');
  int secondSlash = topicStr.indexOf('/', firstSlash + 1);

  if (firstSlash != -1 && secondSlash != -1) {
    int devID = topicStr.substring(firstSlash + 1, secondSlash).toInt();
    if (devID >= 1 && devID <= NUM_DEVICES) {
      int idx = devID - 1;
      
      if (topicStr.endsWith("/bpm")) devices[idx].bpm = val;
      else if (topicStr.endsWith("/spo2")) devices[idx].spo2 = val;

      devices[idx].isOnline = true;
      devices[idx].lastUpdate = millis();
      evaluateDeviceStatus(idx);
    }
  }
}

// ==================== HEALTH LOGIC ENGINE ====================
void evaluateDeviceStatus(int id) {
  int bpm = devices[id].bpm;
  int spo2 = devices[id].spo2;

  if (!devices[id].isOnline) {
    devices[id].status = STATUS_OFFLINE;
  }
  else if ((bpm < 50 || bpm > 120 || spo2 < 94) && spo2 > 0) {
    devices[id].status = STATUS_CRITICAL;
  } 
  else if (((bpm >= 50 && bpm <= 59) || (bpm >= 101 && bpm <= 120) || (spo2 >= 94 && spo2 <= 95)) && spo2 > 0) {
    devices[id].status = STATUS_WARNING;
  } 
  else {
    devices[id].status = STATUS_NORMAL;
  }
}

// ==================== DISPLAY & ALERT LOGIC ====================
void updateDisplayAndAlerts() {
  HealthStatus highestStatus = STATUS_NORMAL;
  int activeAlertDev = -1;

  // ค้นหาเครื่องที่มีสถานะเสี่ยงวิกฤตที่สุด
  for (int i = 0; i < NUM_DEVICES; i++) {
    if (devices[i].status == STATUS_CRITICAL) {
      highestStatus = STATUS_CRITICAL;
      activeAlertDev = i;
      break;
    } else if (devices[i].status == STATUS_WARNING && highestStatus != STATUS_CRITICAL) {
      highestStatus = STATUS_WARNING;
      activeAlertDev = i;
    }
  }

  // สลับหน้าแสดงผลสถานะทุกๆ 3 วินาที
  unsigned long now = millis();
  if (now - lastPageSwitch >= 3000) {
    lastPageSwitch = now;
    currentPage = (currentPage == 0) ? 1 : 0;
  }

  // --- บรรทัดที่ 1: แสดงสถานะ Online / Offline เรียงตามเครื่อง ---
  lcd.setCursor(0, 0);
  if (currentPage == 0) {
    // แสดง W1, W2, W3
    lcd.print("W1:"); lcd.print(devices[0].isOnline ? "ON " : "OFF");
    lcd.print("W2:"); lcd.print(devices[1].isOnline ? "ON " : "OFF");
    lcd.print("W3:"); lcd.print(devices[2].isOnline ? "ON" : "OFF");
  } else {
    // แสดง W4, W5
    lcd.print("W4:"); lcd.print(devices[3].isOnline ? "ON " : "OFF");
    lcd.print("W5:"); lcd.print(devices[4].isOnline ? "ON " : "OFF");
    lcd.print("      "); // เคลียร์พื้นที่ว่างท้ายบรรทัด
  }

  // --- บรรทัดที่ 2: แสดงรายละเอียดค่า BPM/SpO2 ของเครื่องที่มีการเตือนภัย หรือเครื่องแรกที่ Online ---
  lcd.setCursor(0, 1);
  int targetDev = (activeAlertDev != -1) ? activeAlertDev : 0;

  if (devices[targetDev].isOnline) {
    lcd.print("W"); lcd.print(targetDev + 1); lcd.print(":");
    lcd.print(devices[targetDev].bpm); lcd.print("b ");
    lcd.print(devices[targetDev].spo2); lcd.print("% ");

    if (devices[targetDev].status == STATUS_CRITICAL) lcd.print("CRIT!");
    else if (devices[targetDev].status == STATUS_WARNING) lcd.print("WARN!");
    else lcd.print("OK   ");
  } else {
    lcd.print("ALL DEVICES READY");
  }

  // ระบบส่งเสียงเตือน
  triggerBuzzer(highestStatus);
}

void triggerBuzzer(HealthStatus highestStatus) {
  unsigned long now = millis();

  if (highestStatus == STATUS_CRITICAL) {
    if (now - lastBuzzerToggle >= 100) {
      lastBuzzerToggle = now;
      buzzerState = !buzzerState;
      if (buzzerState) tone(BUZZER_PIN, 1200);
      else noTone(BUZZER_PIN);
    }
  } 
  else if (highestStatus == STATUS_WARNING) {
    if (now - lastBuzzerToggle >= 500) {
      lastBuzzerToggle = now;
      buzzerState = !buzzerState;
      if (buzzerState) tone(BUZZER_PIN, 600);
      else noTone(BUZZER_PIN);
    }
  } 
  else {
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
  }
}
