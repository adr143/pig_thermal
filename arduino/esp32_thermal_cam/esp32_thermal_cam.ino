/*
 * ESP32 Thermal Camera Node (I2C sensor, USB serial output)
 * ---------------------------------------------------------------
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
// ---- I2C bus ----
#ifndef I2C_SDA
  #define I2C_SDA       21
#endif
#ifndef I2C_SCL
  #define I2C_SCL       22
#endif
#ifndef I2C_FREQ_HZ
  #define I2C_FREQ_HZ   100000      // 100 kHz
#endif

#ifndef PS_PIN
  #define PS_PIN        4
#endif

#ifndef MLX_I2C_ADDR
  #define MLX_I2C_ADDR  0x33
#endif

#ifndef MLX_REFRESH
  #define MLX_REFRESH   MLX90640_2_HZ
#endif

#define PIXEL_COUNT         768     // 32 x 24
#define FRAME_INTERVAL_MS   250     // throttle serial send rate

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
Adafruit_MLX90640 mlx;

static float      frameBuf[PIXEL_COUNT];
bool              mlxReady = false;

uint32_t          lastSendMs          = 0;
uint32_t          lastDiagnosticsMs   = 0;
uint32_t          validSensorFrames   = 0;
uint32_t          i2cErrors           = 0;
uint32_t          sentFrames          = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
bool initMLX();
void scanI2C();
bool readAndSendFrame();

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("[boot] ESP32 Thermal Camera Node (USB serial mode)"));

  // ---- Drive GY-MCU90640 PS pin LOW for I2C mode ----
  #if PS_PIN >= 0
    pinMode(PS_PIN, OUTPUT);
    digitalWrite(PS_PIN, LOW);
    Serial.printf("[boot] PS pin GPIO%d -> LOW (I2C mode)\n", PS_PIN);
    delay(100);
  #endif

  // ---- I2C init ----
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ_HZ);
  Wire.setTimeOut(100);  // Prevent clock stretching timeouts
  Serial.printf("[boot] I2C ready  SDA=GPIO%d  SCL=GPIO%d  freq=%u Hz\n",
                I2C_SDA, I2C_SCL, I2C_FREQ_HZ);

  // ---- Scan bus first to see what's visible ----
  scanI2C();

  // ---- MLX90640 init ----
  mlxReady = initMLX();

}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  if (mlxReady) {
    readAndSendFrame();
  }

  // Diagnostics every 5 s
  if ((millis() - lastDiagnosticsMs) > 5000) {
    lastDiagnosticsMs = millis();
    Serial.printf("[diag] sensor_frames=%lu i2c_errors=%lu serial_frames_sent=%lu mlx_ok=%s\n",
                  (unsigned long)validSensorFrames,
                  (unsigned long)i2cErrors,
                  (unsigned long)sentFrames,
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
    return false;
  }
  Serial.println(F("OK"));

  mlx.setMode(MLX90640_INTERLEAVED);
  mlx.setResolution(MLX90640_ADC_18BIT);
  mlx.setRefreshRate(MLX_REFRESH);

  // Prime sensor
  Serial.println(F("[mlx] priming (discarding first frame) ..."));
  float dummy[PIXEL_COUNT];
  (void)mlx.getFrame(dummy);
  delay(50);

  Serial.println(F("[mlx] ready (mode=interleaved, adc=18bit)"));
  return true;
}

void scanI2C() {
  Serial.println(F("[i2c] Scanning I2C bus..."));
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c] found device at 0x%02X\n", addr);
      count++;
    }
  }
  Serial.printf("[i2c] Scan complete. Found %d devices.\n", count);
}

// ---------------------------------------------------------------------------
// Frame read + serial output
// ---------------------------------------------------------------------------
bool readAndSendFrame() {
  int rc = mlx.getFrame(frameBuf);
  if (rc != 0) {
    i2cErrors++;
    return false;
  }
  validSensorFrames++;

  if ((millis() - lastSendMs) < FRAME_INTERVAL_MS) {
    return true;
  }
  lastSendMs = millis();

  // Ambient proxy (median of 8 coldest pixels)
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
      dtostrf(frameBuf[i], 0, 1, buf);
      out += buf;
      if (i < PIXEL_COUNT - 1) out += ',';
    }
  }
  out += "]}";

  Serial.println(out);
  sentFrames++;
  return true;
}