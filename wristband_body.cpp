/**
 * Project: ESP32 IoT Health Monitoring Wristband
 * File: wristband_body.cpp (Transmitter Unit)
 * Target MCU: ESP32-C3 Supermini
 * 
 * Hardware Pinout:
 *   - MAX30102 SDA  -> GPIO 8
 *   - MAX30102 SCL  -> GPIO 9
 *   - MAX30102 VIN  -> 3.3V
 *   - MAX30102 GND  -> GND
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ==================== CONFIGURATION ====================
// 1. Wi-Fi Settings
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 2. HiveMQ Cloud Settings
const char* MQTT_SERVER   = "YOUR_HIVEMQ_HOST.hivemq.cloud";
const int   MQTT_PORT     = 8883; // TLS Port
const char* MQTT_USER     = "YOUR_MQTT_USERNAME";
const char* MQTT_PASS     = "YOUR_MQTT_PASSWORD";

// 3. MQTT Topics
const char* TOPIC_BPM     = "wristband/health/bpm";
const char* TOPIC_SPO2    = "wristband/health/spo2";
const char* TOPIC_STATUS  = "wristband/system/status";

// 4. Hardware Pinout Definition (ESP32-C3 Supermini)
#define I2C_SDA 8
#define I2C_SCL 9

// ==================== GLOBAL OBJECTS ====================
MAX30105 particleSensor;
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// ==================== SYSTEM VARIABLES ====================
// Timing Variables
unsigned long lastMqttRetry = 0;
unsigned long lastPublishTime = 0;
const unsigned long PUBLISH_INTERVAL = 5000; // ส่งข้อมูลทุกๆ 5 วินาที

// Health Calculation Variables
const byte RATE_SIZE = 5; // คำนวณค่าเฉลี่ยจาก 5 ค่าล่าสุด
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

float beatsPerMinute = 0.0;
int beatAvg = 0;
int calculatedSpO2 = 0;

// Filter Variables
float filteredBPM = 0.0;
const float ALPHA = 0.3; // EMA Smooth Factor (0.1 - 0.5)

// ==================== FUNCTION DECLARATIONS ====================
void setupWiFi();
void reconnectMQTT();
void readAndProcessSensor();
void publishHealthData();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[INIT] Starting ESP32-C3 Health Wristband (Body)...");

  // 1. Initialize I2C Bus for ESP32-C3
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); // 400kHz Fast Mode

  // 2. Initialize MAX30102 Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[ERROR] MAX30102 sensor was not found. Please check wiring!");
    while (1) {
      delay(500); // Stop execution if sensor missing
    }
  }

  // 3. Configure Sensor Parameters for High Accuracy
  byte ledBrightness = 60; // Options: 0=Off to 255=50mA
  byte sampleAverage = 4;  // Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2;        // Options: 1 = Red only, 2 = Red + IR
  byte sampleRate = 100;   // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;    // Options: 69, 118, 215, 411
  int adcRange = 4096;     // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate power
  particleSensor.setPulseAmplitudeGreen(0);  // Turn off Green LED

  // 4. Setup Secure Network
  espClient.setInsecure(); // Skip certificate verification for HiveMQ Cloud TLS
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  setupWiFi();
}

// ==================== MAIN LOOP ====================
void loop() {
  // 1. Maintain Network Connection
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttRetry > 5000) { // Non-blocking retry every 5 seconds
      lastMqttRetry = now;
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // 2. Read Sensor Signal Real-time
  readAndProcessSensor();

  // 3. Publish Data Periodically
  if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = millis();
    publishHealthData();
  }
}

// ==================== NETWORK FUNCTIONS ====================
void setupWiFi() {
  delay(10);
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Connection Failed. Will retry in loop.");
  }
}

void reconnectMQTT() {
  Serial.print("[MQTT] Attempting TLS Connection...");
  String clientId = "ESP32C3-Wristband-";
  clientId += String(random(0xffff), HEX);

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println(" Connected!");
    mqttClient.publish(TOPIC_STATUS, "ONLINE");
  } else {
    Serial.print(" Failed, rc=");
    Serial.println(mqttClient.state());
  }
}

// ==================== SENSOR PROCESSING ====================
void readAndProcessSensor() {
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  // Check finger detection threshold
  if (irValue < 50000) {
    // Finger removed - Reset values
    beatAvg = 0;
    calculatedSpO2 = 0;
    filteredBPM = 0;
    return;
  }

  // Heartbeat Detection Algorithm
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    // Filter Outliers for age 10-20 (Valid Range: 40 - 180 BPM)
    if (beatsPerMinute >= 40 && beatsPerMinute <= 180) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      // Calculate Moving Average
      int beatSum = 0;
      for (byte x = 0; x < RATE_SIZE; x++) {
        beatSum += rates[x];
      }
      beatAvg = beatSum / RATE_SIZE;

      // Apply Exponential Moving Average (EMA) for extra smooth data
      if (filteredBPM == 0) filteredBPM = beatAvg;
      else filteredBPM = (ALPHA * beatAvg) + ((1.0 - ALPHA) * filteredBPM);
    }
  }

  // SpO2 Estimation Logic (Ratio of Ratios Algorithm)
  if (irValue > 50000 && redValue > 50000) {
    double R = ((double)redValue / (double)irValue);
    // Standard Oximeter Calibration Curve Approximation: SpO2 = 104 - 17 * R
    int spo2Val = (int)(104 - (17 * R));

    // Clamp values within realistic human boundaries
    if (spo2Val > 100) spo2Val = 100;
    if (spo2Val < 80) spo2Val = 80;

    calculatedSpO2 = spo2Val;
  }
}

// ==================== DATA PUBLISHING ====================
void publishHealthData() {
  if (!mqttClient.connected()) return;

  int finalBPM = (int)filteredBPM;
  int finalSpO2 = calculatedSpO2;

  // Publish BPM
  char bpmStr[8];
  itoa(finalBPM, bpmStr, 10);
  mqttClient.publish(TOPIC_BPM, bpmStr);

  // Publish SpO2
  char spo2Str[8];
  itoa(finalSpO2, spo2Str, 10);
  mqttClient.publish(TOPIC_SPO2, spo2Str);

  // Debug Serial Output
  Serial.print("[DATA SENT] BPM: ");
  Serial.print(finalBPM);
  Serial.print(" | SpO2: ");
  Serial.print(finalSpO2);
  Serial.println("%");
}
