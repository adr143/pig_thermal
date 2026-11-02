/*
 * ESP32 Thermal Camera Node (I2C variant)
 * ---------------------------------------------------------------
 * Hardware:
 *   - ESP32-D (DevKit V1 or similar)
 *   - GY-MCU90640 (MLX90640 thermal module, I2C interface)
 *
 * Wiring (GY-MCU90640 -> ESP32):
 *   VCC  -> 3V3           (do NOT use 5V on SDA/SCL — ESP32 is NOT 5V tolerant)
 *   GND  -> GND
 *   SDA  -> ESP32 GPIO21  (I2C SDA, default)
 *   SCL  -> ESP32 GPIO22  (I2C SCL, default)
 *   PS   -> ESP32 GPIO4   (Protocol Select: LOW = I2C mode)
 *                          If your module already has a solder bridge tying
 *                          PS to GND, set PS_PIN to -1 below.
 *
 *   Pull-ups: the GY-MCU90640 board usually has 4.7k pull-ups on SDA/SCL.
 *             If your bus is long or noisy, add external 4.7k to 3V3.
 *
 * Network:
 *   - Joins Wi-Fi (set SSID/PASSWORD below or via secrets.h)
 *   - Opens a WebSocket client to ws://<SERVER_IP>:<SERVER_PORT>/ws/thermal
 *   - Reads MLX90640 32x24 frames over I2C and sends each frame as JSON:
 *       {"type":"frame","t":<ms>,"ambient":<C>,"temps":[768 floats]}
 *
 * Required libraries (Arduino Library Manager):
 *   - Adafruit MLX90640 Library   (>= 1.2.0)
 *   - ArduinoWebsockets           by Gil Maimon  (>= 0.1.3)
 *   - ArduinoJson                 by Benoit Blanchon (>= 6.21)
 *
 * Upload notes:
 *   - Board: "ESP32 Dev Module"
 *   - CPU Freq: 240 MHz
 *   - Flash size: 4 MB
 *   - Partition: "Default 4 MB with spiffs" or "Huge APP"
 *
 * IMPORTANT — memory-safety note (root cause of a previous panic):
 *   The ArduinoWebsockets library and Adafruit_MLX90640 both keep internal
 *   static buffers. If a WebSocket event handler writes through a shared/
 *   global buffer, it can clobber the Wire/MLX state and trigger an ESP32
 *   panic/reboot that looks exactly like an I2C error storm.
 *   FIX APPLIED: any buffer used inside onWsEvent()/onWsMessage() MUST be
 *   declared locally (stack-allocated) inside the handler, so each event
 *   gets a fresh allocation and cannot corrupt shared state.
 *
 * NOTE on blocking:
 *   Adafruit_MLX90640::getFrame() blocks until a fresh frame is ready.
 *   At 4 Hz that is up to 250 ms per call. WebSocket messages are buffered
 *   by the TCP stack during that time and drained by client.poll() on the
 *   next loop iteration. If you need snappier WS response, bump MLX_REFRESH
 *   to MLX90640_8HZ or MLX90640_16HZ and lower FRAME_INTERVAL_MS accordingly.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
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

// ---- I2C bus ----
#ifndef I2C_SDA
  #define I2C_SDA       21          // ESP32 default I2C SDA
#endif
#ifndef I2C_SCL
  #define I2C_SCL       22          // ESP32 default I2C SCL
#endif
#ifndef I2C_FREQ_HZ
  #define I2C_FREQ_HZ   400000      // 400 kHz Fast Mode; MLX90640 supports up to 1 MHz
#endif

// ---- GY-MCU90640 PS pin (Protocol Select) ----
//   PS = LOW  -> I2C mode  (what we want here)
//   PS = HIGH -> UART mode (the original firmware)
// Set PS_PIN to -1 if PS is hard-wired to GND via the board's solder jumper.
#ifndef PS_PIN
  #define PS_PIN        4
#endif

// ---- MLX90640 chip ----
#ifndef MLX_I2C_ADDR
  #define MLX_I2C_ADDR  0x33        // MLX90640 default 7-bit address
#endif

// Refresh rate enum (Adafruit MLX90640 library, note the trailing _HZ):
//   MLX90640_0_5_HZ, MLX90640_1_HZ, MLX90640_2_HZ, MLX90640_4_HZ,
//   MLX90640_8_HZ,   MLX90640_16_HZ, MLX90640_32_HZ, MLX90640_64_HZ
#ifndef MLX_REFRESH
  #define MLX_REFRESH   MLX90640_4_HZ
#endif

#define PIXEL_COUNT         768     // 32 x 24
#define FRAME_INTERVAL_MS   250     // throttle WS send rate (4 Hz at MLX90640_4_HZ)

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WebsocketsClient  client;
Adafruit_MLX90640 mlx;

static float      frameBuf[PIXEL_COUNT];
bool              mlxReady = false;

uint32_t          lastSendMs          = 0;
uint32_t          lastHeartbeatMs     = 0;
uint32_t          lastDiagnosticsMs   = 0;
uint32_t          validSensorFrames   = 0;
uint32_t          i2cErrors           = 0;
uint32_t          sentFrames          = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void onWsEvent(WebsocketsEvent event, String data);
void onWsMessage(WebsocketsMessage message);
void connectWiFi();
void connectWebSocket();
bool initMLX();
void scanI2C();
bool readAndSendFrame();
void sendHeartbeat();

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("[boot] ESP32 Thermal Camera Node (I2C mode)"));

  // ---- Drive GY-MCU90640 PS pin LOW for I2C mode ----
  #if PS_PIN >= 0
    pinMode(PS_PIN, OUTPUT);
    digitalWrite(PS_PIN, LOW);
    Serial.printf("[boot] PS pin GPIO%d -> LOW (I2C mode)\n", PS_PIN);
    delay(50);   // let the module's level shifter settle
  #else
    Serial.println(F("[boot] PS_PIN disabled — verify PS is tied to GND on the board"));
  #endif

  // ---- I2C init ----
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
  Serial.printf("[boot] I2C ready  SDA=GPIO%d  SCL=GPIO%d  freq=%u Hz\n",
                I2C_SDA, I2C_SCL, I2C_FREQ_HZ);

  // ---- MLX90640 init ----
  mlxReady = initMLX();
  if (!mlxReady) {
    Serial.println(F("[boot] MLX90640 not responding — scanning I2C bus for clues..."));
    scanI2C();
  }

  // ---- Wi-Fi + WS ----
  connectWiFi();

  client.onEvent(onWsEvent);
  client.onMessage(onWsMessage);
  client.setInsecure();
  connectWebSocket();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  client.poll();

  // Poll MLX for a new frame and send it (throttled to FRAME_INTERVAL_MS).
  // getFrame() blocks until a fresh frame is ready; see file header note.
  if (mlxReady) {
    readAndSendFrame();
  }

  // Reconnect WebSocket if dropped
  if (!client.available()) {
    static uint32_t lastReconnect = 0;
    if ((millis() - lastReconnect) > 5000) {
      lastReconnect = millis();
      Serial.println(F("[ws] not connected, retrying..."));
      connectWebSocket();
    }
  }

  // Heartbeat every 10 s to keep reverse proxies happy
  if ((millis() - lastHeartbeatMs) > 10000) {
    lastHeartbeatMs = millis();
    if (client.available()) sendHeartbeat();
  }

  // Diagnostics every 5 s
  if ((millis() - lastDiagnosticsMs) > 5000) {
    lastDiagnosticsMs = millis();
    Serial.printf("[diag] sensor_frames=%lu i2c_errors=%lu ws_frames_sent=%lu ws_connected=%s mlx_ok=%s\n",
                  (unsigned long)validSensorFrames,
                  (unsigned long)i2cErrors,
                  (unsigned long)sentFrames,
                  client.available() ? "yes" : "no",
                  mlxReady ? "yes" : "no");
  }
}

// ---------------------------------------------------------------------------
// MLX90640 init
// ---------------------------------------------------------------------------
bool initMLX() {
  Serial.printf("[mlx] probing 0x%02X ... ", MLX_I2C_ADDR);
  if (!mlx.begin(MLX_I2C_ADDR, &Wire)) {
    Serial.println(F("NOT FOUND"));
    Serial.println(F("[mlx] check wiring / PS pin / pull-ups / 3V3 power"));
    return false;
  }
  Serial.println(F("OK"));

  // Configure the sensor. Defaults: chess mode + 18-bit ADC are good for most uses.
  mlx.setMode(MLX90640_CHESS);          // chess mode = better image quality
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX_REFRESH);

  // Throw away the first frame so the next getFrame() returns fresh, valid data
  Serial.println(F("[mlx] priming (discarding first frame) ..."));
  float dummy[PIXEL_COUNT];
  (void)mlx.getFrame(dummy);
  delay(50);

  const char* rateStr =
    MLX_REFRESH == MLX90640_0_5_HZ ? "0.5Hz" :
    MLX_REFRESH == MLX90640_1_HZ   ? "1Hz"   :
    MLX_REFRESH == MLX90640_2_HZ   ? "2Hz"   :
    MLX_REFRESH == MLX90640_4_HZ   ? "4Hz"   :
    MLX_REFRESH == MLX90640_8_HZ   ? "8Hz"   :
    MLX_REFRESH == MLX90640_16_HZ  ? "16Hz"  :
    MLX_REFRESH == MLX90640_32_HZ  ? "32Hz"  : "64Hz";

  Serial.printf("[mlx] ready  refresh=%s  addr=0x%02X  mode=chess  adc=18bit\n",
                rateStr, MLX_I2C_ADDR);
  return true;
}

// I2C bus scan — handy when MLX init fails. Prints every responding address.
void scanI2C() {
  Serial.println(F("[i2c] scanning bus 0..127 ..."));
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c]   found device at 0x%02X\n", addr);
      count++;
    }
  }
  Serial.printf("[i2c] scan done, %u device(s) found\n", count);
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
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000) {
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
    client.send("{\"type\":\"hello\",\"device\":\"esp32-thermal\",\"fw\":2,\"iface\":\"i2c\"}");
  } else {
    Serial.println(F("[ws] connect failed"));
  }
}

// ---------------------------------------------------------------------------
// WebSocket event / message handlers
// ---------------------------------------------------------------------------
// MEMORY-SAFETY RULE: any buffer used inside these handlers MUST be declared
// locally (stack-allocated) inside the function. Do NOT write into globals or
// statics from here — that was the root cause of the previous ESP32 panic
// (illegal memory write clobbering Wire/MLX state). A local buffer is
// re-declared on every event, so even if a write goes past the end it hits
// throwaway stack memory, not shared state.
// ---------------------------------------------------------------------------
void onWsEvent(WebsocketsEvent event, String data) {
  // NOTE: 'data' is already a stack-local String owned by the caller.
  // Do not retain pointers to it after this function returns.
  switch (event) {
    case WebsocketsEvent::ConnectionOpened:
      Serial.println(F("[ws] opened"));
      break;
    case WebsocketsEvent::ConnectionClosed:
      Serial.println(F("[ws] closed"));
      break;
    default:
      // Other events ignored safely
      break;
  }
}

void onWsMessage(WebsocketsMessage message) {
  // NOTE: 'message' is a stack-local object owned by the caller.
  // Do not retain pointers to its data after this function returns.
  Serial.printf("[ws] server: %s\n", message.data().c_str());
}

void sendHeartbeat() {
  // StaticJsonDocument here is fine — this is called from loop(), not from
  // a WS event handler, so there is no re-entrancy concern.
  StaticJsonDocument<160> doc;
  doc["type"]      = "heartbeat";
  doc["uptime_s"]  = (uint32_t)(millis() / 1000);
  doc["rssi"]      = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["mlx_ok"]    = mlxReady;
  String out;
  serializeJson(doc, out);
  client.send(out);
}

// ---------------------------------------------------------------------------
// Frame read + send
// ---------------------------------------------------------------------------
bool readAndSendFrame() {
  // Adafruit_MLX90640::getFrame() blocks until a fresh full frame is ready,
  // then applies calibration and writes 768 compensated pixel temps (in deg C)
  // into frameBuf. Returns 0 on success.
  int rc = mlx.getFrame(frameBuf);
  if (rc != 0) {
    i2cErrors++;
    return false;
  }
  validSensorFrames++;

  // Throttle the WS send rate independently of the sensor rate, so a higher
  // MLX_REFRESH does not flood the server.
  if ((millis() - lastSendMs) < FRAME_INTERVAL_MS) {
    return true;
  }
  lastSendMs = millis();

  // ---- Ambient temperature estimate ----
  // The Adafruit library does not expose the MLX90640's internal Ta register
  // (PTAT, RAM word 0x0720) through its public API. As a robust proxy we use
  // the median of the 8 coldest pixels of the WHOLE frame — these are
  // background pixels far from any warm body, which closely tracks the
  // chip/housing ambient.
  // If you need the true Ta, read register 0x0720 manually with Wire and
  // apply the MLX90640 PTAT formula using KPTAT / PTAT25 from EEPROM.
  //
  // Implementation: partial selection sort on a copy of frameBuf, advancing
  // only 8 positions. O(8 * 768) = ~6k comparisons, negligible vs I2C time.
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
  // ambientBuf[0..7] are now the 8 coldest pixels in ascending order.
  // Take the median of those (index 3).
  float ambient = ambientBuf[3];

  // ---- Build JSON manually ----
  // NOTE: we deliberately do NOT use serializeJson() for the header here,
  // because it emits a closing '}', which would produce invalid JSON once
  // we append the temps array. Building the string directly is also faster
  // and uses less heap than a JsonDocument large enough for 768 floats.
  String out;
  out.reserve(4096);
  out += "{\"type\":\"frame\",\"t\":";
  out += (uint32_t)millis();
  out += ",\"ambient\":";
  {
    char abuf[16];
    snprintf(abuf, sizeof(abuf), "%.2f", ambient);
    out += abuf;
  }
  out += ",\"temps\":[";
  {
    char buf[16];
    for (size_t i = 0; i < PIXEL_COUNT; i++) {
      // dtostrf is faster on ESP32 than snprintf for floats
      dtostrf(frameBuf[i], 0, 1, buf);
      out += buf;
      if (i < PIXEL_COUNT - 1) out += ',';
    }
  }
  out += "]}";

  if (!client.send(out)) {
    Serial.println(F("[ws] send failed"));
    return false;
  }
  sentFrames++;
  return true;
}
