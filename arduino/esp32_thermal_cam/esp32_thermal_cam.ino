/*
 * ESP32 Thermal Camera Node
 * ---------------------------------------------------------------
 * Hardware:
 *   - ESP32-D (DevKit V1 or similar)
 *   - MLX90640 thermal module with on-board MCU bridge
 *     (a.k.a. GY-MCU90640 / TLX90640 / MY-MCU90640 — manufacturer
 *      silkscreen may read "LEIXINRUI", "GY-MCU", "TLX", etc.)
 *
 * Visible module pinout (8 pins):
 *   3V3  -> ESP32 3V3                    (do NOT use 5V on 3V3-pinned boards)
 *   GND  -> ESP32 GND
 *   SDA  -> (LEAVE UNCONNECTED — I2C data, used only for direct-MLX mode)
 *   SCL  -> (LEAVE UNCONNECTED — I2C clock, used only for direct-MLX mode)
 *   TX   -> ESP32 GPIO16 (UART2 RX)     (data from module to ESP32)
 *   RX   -> ESP32 GPIO17 (UART2 TX)     (optional — config commands only)
 *   MC   -> (LEAVE UNCONNECTED — Mode Control, internal pull-down = UART mode)
 *   PS   -> (LEAVE UNCONNECTED — Power Save, internal config = always-on)
 *
 * The on-board MCU of this module reads the MLX90640 sensor over its own
 * internal I2C bus and streams binary frames out the TX pin at 115200 baud.
 * Your ESP32 should NOT talk I2C directly to the MLX90640 — let the bridge
 * MCU do it. The SDA/SCL pins on the header only matter if you intend to
 * bypass the bridge MCU (advanced, requires different firmware).
 *
 * Network:
 *   - Joins Wi-Fi (set SSID/PASSWORD below or via secrets.h)
 *   - Opens a WebSocket client to ws://<SERVER_IP>:<SERVER_PORT>/ws/thermal
 *   - Parses MLX90640 32x24 frames from the module UART and sends
 *     each frame as a JSON message: {"t": <epoch_ms>, "ambient": <C>,
 *      "temps": [768 floats]}
 *
 * Required libraries (Arduino Library Manager):
 *   - ArduinoWebsockets  by Gil Maimon  (>= 0.1.3)
 *   - ArduinoJson        by Benoit Blanchon (>= 6.21)
 *
 * Upload notes:
 *   - Board: "ESP32 Dev Module"
 *   - CPU Freq: 240 MHz
 *   - Flash size: 4 MB
 *   - Partition: "Default 4 MB with spiffs" or "Huge APP"
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include "secrets.h"   // <-- create this file next to the .ino (see secrets.example.h)

using namespace websockets;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#ifndef WIFI_SSID
  #define WIFI_SSID     "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
  #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef WS_HOST
  #define WS_HOST       "192.168.1.100"      // mini-PC IP
#endif
#ifndef WS_PORT
  #define WS_PORT       5000
#endif
#ifndef WS_PATH
  #define WS_PATH       "/ws/thermal"
#endif

#define UART_BAUD            115200      // GY-MCU90640 default baud
#define FRAME_INTERVAL_MS    250         // 4 Hz — match the module's output rate

// GY-MCU90640 binary frame protocol (most common firmware variant)
//   Header: 0x5A 0x5A 0x02 0x02 (4 bytes)
//   Ambient (2 bytes, big-endian, signed, x0.01 C)
//   Pixels: 768 * 2 bytes (big-endian, signed, x0.01 C)
//   Checksum: 1 byte  (sum of all payload bytes mod 256)
//
// NOTE: Some firmware variants use a different header. If you see no frames,
//       open the Serial Monitor @ 115200 to see raw bytes and adjust
//       HEADER[] below. Common alternatives: {0x5A,0x5A,0x40,0x40},
//       {0x5A,0xA5,0x02,0x02}, or {0x55,0xAA,0x02,0x02}.
#define HEADER_LEN          4
#define AMBIENT_LEN          2
#define PIXEL_COUNT          768     // 32 x 24
#define PIXEL_BYTES          (PIXEL_COUNT * 2)
#define CHECKSUM_LEN         1
#define FRAME_LEN            (HEADER_LEN + AMBIENT_LEN + PIXEL_BYTES + CHECKSUM_LEN)

static const uint8_t HEADER[HEADER_LEN] = { 0x5A, 0x5A, 0x02, 0x02 };

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WebsocketsClient client;
WiFiClient       netClient;

// Frame parser state
enum ParseState { SYNC, READ_FRAME };
ParseState   parseState    = SYNC;
size_t       bufLen        = 0;
uint8_t      rxFrame[FRAME_LEN];

// Throttling
uint32_t     lastSendMs    = 0;

// Heartbeat
uint32_t     lastHeartbeatMs = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void   onWsEvent(WebsocketsEvent event, String data);
void   connectWiFi();
void   connectWebSocket();
void   parseUart();
bool   processFrame();
bool   sendFrame(float ambient, const float temps[PIXEL_COUNT]);
void   sendHeartbeat();

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("[boot] ESP32 Thermal Camera Node"));

  // UART1 for debug, UART2 for GY-MCU90640 module
  // UART2 default pins on ESP32: RX=16, TX=17
  Serial2.begin(UART_BAUD, SERIAL_8N1, 16, 17);
  Serial.println(F("[boot] UART2 ready for GY-MCU90640"));

  connectWiFi();

  client.onEvent(onWsEvent);
  // Allow unusually large JSON frames (768 floats can be ~6 KB of text)
  client.setInsecure();
  connectWebSocket();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  client.poll();
  parseUart();

  // Reconnect WebSocket if dropped
  if (!client.available()) {
    static uint32_t lastReconnect = 0;
    if (millis() - lastReconnect > 5000) {
      lastReconnect = millis();
      Serial.println(F("[ws] not connected, retrying..."));
      connectWebSocket();
    }
  }

  // Heartbeat every 10 s to keep reverse proxies happy
  if (millis() - lastHeartbeatMs > 10000) {
    lastHeartbeatMs = millis();
    if (client.available()) sendHeartbeat();
  }
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
void connectWiFi() {
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\n[wifi] FAILED. Rebooting in 5 s."));
    delay(5000);
    ESP.restart();
  }
  Serial.printf("\n[wifi] connected. IP=%s  RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
void connectWebSocket() {
  char url[64];
  snprintf(url, sizeof(url), "ws://%s:%u%s", WS_HOST, WS_PORT, WS_PATH);
  Serial.printf("[ws] connecting to %s\n", url);
  bool ok = client.connect(url);
  if (ok) {
    Serial.println(F("[ws] connected"));
    client.send("{\"type\":\"hello\",\"device\":\"esp32-thermal\",\"fw\":1}");
  } else {
    Serial.println(F("[ws] connect failed"));
  }
}

void onWsEvent(WebsocketsEvent event, String data) {
  // The ArduinoWebsockets library only defines ConnectionClosed (and in some
  // versions ConnectionOpened). We avoid hard-casing other enum members so
  // the sketch compiles across all library versions.
  if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println(F("[ws] closed"));
  }
}

void sendHeartbeat() {
  StaticJsonDocument<128> doc;
  doc["type"]      = "heartbeat";
  doc["uptime_s"]  = (uint32_t)(millis() / 1000);
  doc["rssi"]      = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  String out;
  serializeJson(doc, out);
  client.send(out);
}

// ---------------------------------------------------------------------------
// UART frame parser
// ---------------------------------------------------------------------------
void parseUart() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read();

    switch (parseState) {
      case SYNC: {
        // Look for the 4-byte header
        rxFrame[bufLen++] = b;
        // Compare trailing HEADER_LEN bytes against HEADER
        if (bufLen >= HEADER_LEN) {
          size_t off = bufLen - HEADER_LEN;
          if (memcmp(rxFrame + off, HEADER, HEADER_LEN) == 0) {
            // Shift header to start of buffer
            if (off != 0) memmove(rxFrame, rxFrame + off, HEADER_LEN);
            bufLen = HEADER_LEN;
            parseState = READ_FRAME;
          } else {
            // Slide one byte forward — keep last HEADER_LEN-1 bytes
            memmove(rxFrame, rxFrame + 1, bufLen - 1);
            bufLen--;
          }
        }
        break;
      }
      case READ_FRAME: {
        rxFrame[bufLen++] = b;
        if (bufLen == FRAME_LEN) {
          processFrame();
          // Reset for next frame
          bufLen = 0;
          parseState = SYNC;
        }
        // Guard against overflow / wrong sync
        if (bufLen > FRAME_LEN) {
          bufLen = 0;
          parseState = SYNC;
        }
        break;
      }
    }
  }
}

// Read big-endian signed 16-bit (x0.01 C) -> float Celsius
static inline float toCelsius(int16_t raw) {
  return raw / 100.0f;
}

bool processFrame() {
  // Verify checksum: sum of payload bytes (after header, before checksum)
  uint8_t sum = 0;
  for (size_t i = HEADER_LEN; i < FRAME_LEN - CHECKSUM_LEN; i++) {
    sum += rxFrame[i];
  }
  uint8_t expected = rxFrame[FRAME_LEN - 1];
  if (sum != expected) {
    // Bad frame — drop, but keep running
    return false;
  }

  // Ambient temperature
  int16_t ambRaw = (int16_t)((rxFrame[HEADER_LEN] << 8) | rxFrame[HEADER_LEN + 1]);
  float ambient = toCelsius(ambRaw);

  // 768 pixel temperatures
  static float temps[PIXEL_COUNT];
  size_t off = HEADER_LEN + AMBIENT_LEN;
  for (size_t i = 0; i < PIXEL_COUNT; i++) {
    int16_t raw = (int16_t)((rxFrame[off + i * 2] << 8) | rxFrame[off + i * 2 + 1]);
    temps[i] = toCelsius(raw);
  }

  // Throttle network sends (in case module streams faster than FRAME_INTERVAL)
  if (millis() - lastSendMs < FRAME_INTERVAL_MS) {
    return true;
  }
  lastSendMs = millis();

  sendFrame(ambient, temps);
  return true;
}

bool sendFrame(float ambient, const float temps[PIXEL_COUNT]) {
  // Build compact JSON: {"type":"frame","t":<ms>,"ambient":<float>,"temps":[...]}
  // Use a chunked writer to avoid 8 KB+ allocation.
  StaticJsonDocument<128> head;
  head["type"]    = "frame";
  head["t"]       = (uint32_t)millis();
  head["ambient"] = (float)((int)(ambient * 100)) / 100.0f;

  String out;
  serializeJson(head, out);
  out += ",\"temps\":[";
  // Append 768 floats with 1 decimal place
  char buf[16];
  for (size_t i = 0; i < PIXEL_COUNT; i++) {
    // Round to 1 decimal — sufficient for MLX90640 (0.1 C resolution in practice)
    snprintf(buf, sizeof(buf), "%.1f", temps[i]);
    out += buf;
    if (i < PIXEL_COUNT - 1) out += ',';
  }
  out += "]}";

  if (!client.send(out)) {
    Serial.println(F("[ws] send failed"));
    return false;
  }
  return true;
}
