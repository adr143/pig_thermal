/*
 * ESP32 Thermal Camera Node (HTTP POST Wi-Fi Mode)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <ArduinoJson.h>
#include "secrets.h"   // Must contain WIFI_SSID and WIFI_PASSWORD

// ---- Configuration ----
#ifndef WS_HOST
  #define WS_HOST       "192.168.0.103"  // Replace with your Mini-PC's IP address
#endif
#define SERVER_URL      "http://" WS_HOST ":5000/thermal"

#define I2C_SDA         21
#define I2C_SCL         22
#define I2C_FREQ_HZ     100000
#define PS_PIN          4
#define MLX_I2C_ADDR    0x33
#define MLX_REFRESH     MLX90640_2_HZ

#define PIXEL_COUNT         768
#define FRAME_INTERVAL_MS   5000    // 5 seconds (change to 10000 for 10s)

Adafruit_MLX90640 mlx;
static float frameBuf[PIXEL_COUNT];
bool mlxReady = false;
uint32_t lastSendMs = 0;

void connectWiFi() {
  Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("[boot] ESP32 Thermal Node (HTTP POST Mode)"));

  pinMode(PS_PIN, OUTPUT);
  digitalWrite(PS_PIN, LOW);
  delay(50);

  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
  Wire.setTimeOut(100);

  if (mlx.begin(MLX_I2C_ADDR, &Wire)) {
    mlxReady = true;
    mlx.setMode(MLX90640_INTERLEAVED);
    mlx.setResolution(MLX90640_ADC_18BIT);
    mlx.setRefreshRate(MLX_REFRESH);
    Serial.println(F("[mlx] Initialized successfully"));
  } else {
    Serial.println(F("[mlx] ERROR: Sensor not found!"));
  }

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

  if (!mlxReady) {
    delay(2000);
    return;
  }

  // Throttle interval (5 to 10 seconds)
  if (millis() - lastSendMs < FRAME_INTERVAL_MS) {
    return;
  }
  lastSendMs = millis();

  if (mlx.getFrame(frameBuf) != 0) {
    Serial.println(F("[error] Frame read failed"));
    return;
  }

  // Calculate ambient proxy (median of 8 coldest pixels)
  float ambientBuf[PIXEL_COUNT];
  memcpy(ambientBuf, frameBuf, sizeof(ambientBuf));
  for (size_t i = 0; i < 8; i++) {
    size_t minIdx = i;
    for (size_t j = i + 1; j < PIXEL_COUNT; j++) {
      if (ambientBuf[j] < ambientBuf[minIdx]) minIdx = j;
    }
    if (minIdx != i) {
      float t = ambientBuf[i];
      ambientBuf[i] = ambientBuf[minIdx];
      ambientBuf[minIdx] = t;
    }
  }
  float ambient = ambientBuf[3];

  // Serialize to JSON
  StaticJsonDocument<16384> doc;
  doc["t"] = (uint32_t)millis();
  doc["ambient"] = ambient;

  JsonArray temps = doc.createNestedArray("temps");
  for (size_t i = 0; i < PIXEL_COUNT; i++) {
    temps.add(frameBuf[i]);
  }

  String payload;
  serializeJson(doc, payload);

  // Send via HTTP POST
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(payload);
  if (httpResponseCode > 0) {
    Serial.printf("[http] POST sent successfully. Response code: %d\n", httpResponseCode);
  } else {
    Serial.printf("[http] POST failed. Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}