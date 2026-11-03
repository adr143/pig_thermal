/*
 * ESP32 Thermal Camera Node (HTTP POST, UART mode with GY-MCU90640)
 * ---------------------------------------------------------------
 * Hardware:
 *   - ESP32-D (DevKit V1 or similar)
 *   - GY-MCU90640 (MLX90640 thermal module, UART interface)
 *
 * Wiring (GY-MCU90640 -> ESP32):
 *   VCC  -> 5V or 3V3  (check your module silkscreen — most GY-MCU90640
 *                        boards want 5V; they have an onboard 3V3 regulator)
 *   GND  -> GND
 *   TX   -> ESP32 GPIO16  (UART2 RX — the module's TX goes to our RX)
 *   RX   -> ESP32 GPIO17  (UART2 TX — only needed if sending commands;
 *                          for receive-only frame streaming, leave it
 *                          disconnected)
 *   PS   -> ESP32 GPIO4   (HIGH = UART mode; or leave disconnected if your
 *                          board has PS pulled HIGH by default)
 *
 *   IMPORTANT: this is the UART mode of the GY-MCU90640. The module's
 *   onboard MCU handles ALL the MLX90640 I2C reads internally and streams
 *   pre-parsed frame data over UART. The ESP32 never touches I2C.
 *   This eliminates the entire class of I2C/WiFi coexistence problems and
 *   Adafruit library panics that plagued the I2C version.
 *
 * Network:
 *   - Joins Wi-Fi (set SSID/PASSWORD in secrets.h)
 *   - POSTs a JSON frame to http://<SERVER_IP>:5000/thermal every FRAME_INTERVAL_MS:
 *       {"t":<ms>,"ambient":<C>,"temps":[768 floats]}
 *
 * No external libraries required beyond the ESP32 Arduino core.
 *
 * Upload notes:
 *   - Board: "ESP32 Dev Module", CPU 240 MHz, Flash 4 MB
 *
 * GY-MCU90640 UART protocol (THIS MODULE'S FIRMWARE VARIANT — decoded empirically):
 *   Baud: 115200
 *   Frame format (little-endian, 517 bytes total):
 *     [0..3]     Header:    0x5A 0x5A 0x02 0x06
 *     [4..515]   Pixels:    256 × int16 LE / 100 = Celsius (downsampled 16x16 image)
 *     [516]      Trailer:   1 byte (purpose unknown — not a standard checksum;
 *                           varies per frame; we IGNORE it and rely on header sync)
 *   Frame rate: ~2 Hz (one 517-byte frame every ~500ms)
 *
 *   IMPORTANT: This format was determined empirically by decoding raw UART
 *   bytes. The standard GY-MCU90640 protocol uses 768 pixels (32x24) at 2
 *   bytes each = 1543 bytes/frame. THIS FIRMWARE IS DIFFERENT — it sends a
 *   downsampled 16x16 = 256 pixel image at 2 bytes each = 517 bytes/frame.
 *   The trailer byte does not match any standard checksum formula (CRC8,
 *   sum, XOR, two's complement all failed). We skip validation and rely on
 *   header sync + sanity-range checking of pixel values.
 *
 *   If you change modules and frames stop arriving, run the
 *   GY_MCU90640_UART_Diagnostic.ino sketch to re-decode the format.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#ifndef WS_HOST
  #define WS_HOST           "192.168.0.103"   // mini-PC IP
#endif
#define SERVER_URL          "http://" WS_HOST ":5000/thermal"

// ---- UART pins ----
#ifndef UART_RX_PIN
  #define UART_RX_PIN       16          // ESP32 GPIO16 = UART2 RX (connect to module TX)
#endif
#ifndef UART_TX_PIN
  #define UART_TX_PIN       17          // ESP32 GPIO17 = UART2 TX (connect to module RX
                                        //   only if sending commands; otherwise leave open)
#endif
#ifndef UART_BAUD
  #define UART_BAUD         115200      // GY-MCU90640 default; some firmware uses 921600
#endif
#ifndef RX_BUFFER_SIZE
  // Must hold at least one full frame (517 bytes) plus headroom for frames
  // arriving during HTTP POST (which can block for up to HTTP_TIMEOUT_MS).
  // At 115200 baud, one frame arrives every ~45ms. A 4s HTTP POST = ~88 frames
  // = ~45KB. We can't buffer all of that, but 4096 bytes (7+ frames) gives
  // ~320ms of headroom — enough for normal POST latency.
  // ESP32 UART driver default is 256 — way too small.
  #define RX_BUFFER_SIZE    4096        // 7+ frames of headroom
#endif

// ---- PS pin (Protocol Select on GY-MCU90640) ----
//   PS = HIGH -> UART mode (the onboard MCU bridges MLX90640 I2C to UART)
//   PS = LOW  -> I2C mode (the MLX90640 I2C bus is exposed directly)
//   Set PS_PIN to -1 if your board has PS hard-wired HIGH (UART-only boards).
#ifndef PS_PIN
  #define PS_PIN            4
#endif

// ---- WiFi ----
#ifndef WIFI_TX_POWER_DBM
  #define WIFI_TX_POWER_DBM   17        // 17 dBm is fine for UART (no I2C to disturb)
#endif

// ---- Timing ----
#define FRAME_INTERVAL_MS     5000      // POST every 5 s (frames arrive at ~2 Hz; we send the latest)
#define HTTP_TIMEOUT_MS       4000      // fail fast if server unreachable
#define NO_FRAME_WARN_MS      5000      // warn if no frame received in this interval
#define NO_FRAME_RECOVER_MS   15000     // restart UART receiver after this long without a valid frame

// ---- GY-MCU90640 binary frame protocol ----
// Most common firmware: 0x5A 0x5A 0x02 0x02 header.
// If your module uses a different header (e.g. 0x5A 0x5A 0x03 0x04),
// change HEADER[] below — the parser auto-syncs to whatever you set.
#ifndef HEADER_BYTE_0
  #define HEADER_BYTE_0      0x5A
#endif
#ifndef HEADER_BYTE_1
  #define HEADER_BYTE_1      0x5A
#endif
#ifndef HEADER_BYTE_2
  #define HEADER_BYTE_2      0x02
#endif
#ifndef HEADER_BYTE_3
  // IMPORTANT: this is firmware-dependent. The diagnostic sketch showed
  // your module uses 0x06, not 0x02 like the "standard" GY-MCU90640 protocol.
  // If you change modules and frames stop arriving, re-run the diagnostic
  // sketch and update this to whatever header it finds.
  #define HEADER_BYTE_3      0x06
#endif

// ---- Byte order ----
// The diagnostic showed your module's firmware uses LITTLE-ENDIAN 16-bit
// values (low byte first). The original "standard" GY-MCU90640 protocol
// uses big-endian, but many firmware variants (including yours) use LE.
// If you change modules and temperatures look impossibly high (>200 C),
// flip this to 0 (big-endian).
#ifndef MLX_BYTE_ORDER_LE
  #define MLX_BYTE_ORDER_LE   1
#endif

// This firmware sends a DOWNSAMPLED 16x16 = 256 pixel image (NOT the full
// 32x24 = 768 pixels). We expand to 768 by nearest-neighbor replication
// so the Flask server gets the array size it expects.
#define RAW_PIXEL_COUNT       256       // pixels per frame from this firmware (16x16)
#define OUTPUT_PIXEL_COUNT    768       // pixels we send to server (32x24, expanded)

#define HEADER_LEN           4
#define PIXEL_BYTES          (RAW_PIXEL_COUNT * 2)   // 256 * 2 = 512 bytes
#define TRAILER_LEN          1                       // 1 byte trailer (purpose unknown)
#define FRAME_LEN            (HEADER_LEN + PIXEL_BYTES + TRAILER_LEN)   // 4 + 512 + 1 = 517

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static const uint8_t HEADER[HEADER_LEN] = { HEADER_BYTE_0, HEADER_BYTE_1,
                                            HEADER_BYTE_2, HEADER_BYTE_3 };

// Latest frame data (written by parser, read by POST)
static float      latestTemps[OUTPUT_PIXEL_COUNT];   // 768 floats (expanded from 256)
static float      latestTempsRaw[RAW_PIXEL_COUNT];    // 256 floats (as received)
static float      latestAmbient   = 0.0f;
static bool       newFrameAvailable = false;
static uint32_t   lastFrameReceivedMs = 0;

// Frame parser state
enum ParseState { SYNC, READ_BODY };
ParseState  parseState = SYNC;
uint8_t     rxFrame[FRAME_LEN];
size_t      rxLen = 0;

// Counters
uint32_t    lastSendMs        = 0;
uint32_t    validFrames       = 0;
uint32_t    checksumErrors    = 0;
uint32_t    postsOk           = 0;
uint32_t    postsFail         = 0;
uint32_t    uartRestarts      = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void connectWiFi();
void parseUart();
bool processFrame();
bool postFrame();
void restartUartReceiver();
void sendConfigCommand(uint8_t cmd);

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("[boot] ESP32 Thermal Node (HTTP POST, UART mode)"));

  // ---- PS pin -> HIGH for UART mode ----
  #if PS_PIN >= 0
    pinMode(PS_PIN, OUTPUT);
    digitalWrite(PS_PIN, HIGH);
    Serial.printf("[boot] PS pin GPIO%d -> HIGH (UART mode)\n", PS_PIN);
    delay(50);
  #else
    Serial.println(F("[boot] PS_PIN disabled — verify PS is HIGH on the board"));
  #endif

  // ---- UART2 init (ESP32 UART2: RX=GPIO16, TX=GPIO17) ----
  Serial2.setRxBufferSize(RX_BUFFER_SIZE);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  lastFrameReceivedMs = millis();
  Serial.printf("[boot] UART2 ready  RX=GPIO%d  TX=GPIO%d  baud=%u  rx_buf=%u\n",
                UART_RX_PIN, UART_TX_PIN, UART_BAUD, RX_BUFFER_SIZE);

  // Give the GY-MCU90640's onboard MCU time to boot and start streaming.
  // The module starts sending frames automatically on power-on (most firmware).
  Serial.println(F("[boot] waiting for GY-MCU90640 to start streaming..."));

  connectWiFi();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
  // ---- Read & parse UART data from GY-MCU90640 ----
  parseUart();

  // ---- Wi-Fi check ----
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[wifi] disconnected, reconnecting..."));
    connectWiFi();
    return;
  }

  // ---- POST throttled to FRAME_INTERVAL_MS ----
  if (newFrameAvailable && (millis() - lastSendMs >= FRAME_INTERVAL_MS)) {
    lastSendMs = millis();
    newFrameAvailable = false;
    postFrame();
  }

  // ---- Warn and restart the UART receiver if valid sensor frames stop ----
  uint32_t frameSilenceMs = millis() - lastFrameReceivedMs;
  if (frameSilenceMs > NO_FRAME_WARN_MS) {
    static uint32_t lastWarnMs = 0;
    if (millis() - lastWarnMs > NO_FRAME_WARN_MS) {
      lastWarnMs = millis();
      Serial.printf("[uart] no valid frame for %lu ms — check wiring/PS pin/baud rate\n",
                    (unsigned long)frameSilenceMs);
    }
    if (frameSilenceMs > NO_FRAME_RECOVER_MS) {
      restartUartReceiver();
    }
  }

  // ---- Periodic diagnostics ----
  static uint32_t lastDiagMs = 0;
  if (millis() - lastDiagMs > 10000) {
    lastDiagMs = millis();
    Serial.printf("[diag] frames=%lu cksum_err=%lu uart_restarts=%lu posts_ok=%lu posts_fail=%lu wifi=%s free_heap=%lu\n",
                  (unsigned long)validFrames,
                  (unsigned long)checksumErrors,
                  (unsigned long)uartRestarts,
                  (unsigned long)postsOk,
                  (unsigned long)postsFail,
                  WiFi.status() == WL_CONNECTED ? "ok" : "BAD",
                  (unsigned long)ESP.getFreeHeap());
  }
}

// ---------------------------------------------------------------------------
// Reinitialize the ESP32 UART receiver and resync to the next sensor frame.
// This resets the serial link/parser, not the thermal module's power.
// ---------------------------------------------------------------------------
void restartUartReceiver() {
  Serial.printf("[uart] no valid frame for %lu ms; restarting UART receiver\n",
                (unsigned long)(millis() - lastFrameReceivedMs));
  Serial2.end();
  delay(100);
  Serial2.setRxBufferSize(RX_BUFFER_SIZE);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  while (Serial2.available()) {
    Serial2.read();
  }
  rxLen = 0;
  parseState = SYNC;
  newFrameAvailable = false;
  lastFrameReceivedMs = millis();
  uartRestarts++;
}

// ---------------------------------------------------------------------------
// UART frame parser (state machine: SYNC -> READ_BODY -> SYNC -> ...)
// ---------------------------------------------------------------------------
void parseUart() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read();

    switch (parseState) {
      case SYNC: {
        rxFrame[rxLen++] = b;
        if (rxLen >= HEADER_LEN) {
          // Look for the header pattern at the end of our buffer
          size_t off = rxLen - HEADER_LEN;
          if (memcmp(rxFrame + off, HEADER, HEADER_LEN) == 0) {
            // Header found — move it to the start of the buffer if needed
            if (off != 0) {
              memmove(rxFrame, rxFrame + off, HEADER_LEN);
            }
            rxLen = HEADER_LEN;
            parseState = READ_BODY;
          } else {
            // Header not found yet — slide window forward by 1 byte
            memmove(rxFrame, rxFrame + 1, rxLen - 1);
            rxLen--;
          }
        }
        break;
      }
      case READ_BODY: {
        rxFrame[rxLen++] = b;
        if (rxLen == FRAME_LEN) {
          processFrame();
          rxLen = 0;
          parseState = SYNC;
        }
        // Overflow guard (should never happen with correct frame length)
        if (rxLen > FRAME_LEN) {
          rxLen = 0;
          parseState = SYNC;
        }
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Process a complete 517-byte frame: parse 256 pixels, expand to 768, estimate ambient
// ---------------------------------------------------------------------------
bool processFrame() {
  // ---- Sanity check: count pixels with sensible temperature values ----
  // We can't use a checksum (the trailer byte doesn't match any standard formula),
  // so instead we count how many of the 256 pixels fall in a plausible range
  // (-20 to +200 C covers any real-world thermal scene with margin). If too many
  // are out of range, the parser desynced and we reject the frame.
  size_t sensible = 0;
  for (size_t i = 0; i < RAW_PIXEL_COUNT; i++) {
    size_t off = HEADER_LEN + i * 2;
    int16_t raw = (int16_t)((rxFrame[off + 1] << 8) | rxFrame[off]);
    float t = raw / 100.0f;
    if (t >= -20.0f && t <= 200.0f) sensible++;
  }
  if (sensible < RAW_PIXEL_COUNT * 8 / 10) {   // require 80% sensible
    checksumErrors++;   // reuse this counter for "bad frame" count
    if (checksumErrors <= 10 || (checksumErrors % 100) == 0) {
      Serial.printf("[uart] sanity check failed: %u/%u pixels in range — desync, rejecting\n",
                    (unsigned)sensible, RAW_PIXEL_COUNT);
    }
    return false;
  }

  validFrames++;
  lastFrameReceivedMs = millis();

  // ---- Parse 256 raw pixels (each 2 bytes, little-endian) ----
  // This firmware sends a 16x16 = 256 pixel downsampled image.
  // Pixel 0 is the ambient (chip housing) temperature.
  // Pixels 1..255 are the 16x16 thermal image (row-major).
  for (size_t i = 0; i < RAW_PIXEL_COUNT; i++) {
    size_t off = HEADER_LEN + i * 2;
    int16_t raw;
    #if MLX_BYTE_ORDER_LE
      raw = (int16_t)((rxFrame[off + 1] << 8) | rxFrame[off]);
    #else
      raw = (int16_t)((rxFrame[off] << 8) | rxFrame[off + 1]);
    #endif
    latestTempsRaw[i] = raw / 100.0f;
  }
  latestAmbient = latestTempsRaw[0];   // pixel 0 = ambient

  // ---- Row fixed-pattern noise correction (MLX90640 row banding fix) ----
  // The MLX90640 sensor has inherent row-to-row gain/offset variation that
  // creates visible horizontal bands/stripes in the image. This is a known
  // characteristic of microbolometer sensors. We normalize each row by:
  //   1. Computing the global mean of all 256 pixels
  //   2. Computing the mean of each of the 16 rows
  //   3. Subtracting (rowMean - globalMean) from each pixel in that row
  // This flattens all rows to the same baseline, eliminating the horizontal
  // banding while preserving the actual thermal image content.
  //
  // We use pixels 1..255 (skipping pixel 0 = ambient) for the mean
  // calculations so the ambient doesn't skew the statistics.
  float globalSum = 0.0f;
  for (size_t i = 1; i < RAW_PIXEL_COUNT; i++) globalSum += latestTempsRaw[i];
  float globalMean = globalSum / (RAW_PIXEL_COUNT - 1);

  for (size_t row = 0; row < 16; row++) {
    float rowSum = 0.0f;
    for (size_t col = 0; col < 16; col++) {
      size_t idx = row * 16 + col;
      if (idx > 0) rowSum += latestTempsRaw[idx];   // skip pixel 0 (ambient)
    }
    // Row 0 has 15 pixels (excluding ambient); other rows have 16
    float rowMean = rowSum / (row == 0 ? 15.0f : 16.0f);
    float rowOffset = rowMean - globalMean;
    for (size_t col = 0; col < 16; col++) {
      size_t idx = row * 16 + col;
      if (idx > 0) latestTempsRaw[idx] -= rowOffset;
    }
  }

  // ---- Expand 16x16 (256 pixels) to 32x24 (768 pixels) by BILINEAR interpolation ----
  // Nearest-neighbor made the image look extremely blocky (visible 2x2 squares).
  // Bilinear interpolation computes a weighted average of the 4 nearest input
  // pixels, producing smooth gradients that look much more like a real thermal
  // image. No real detail is added — it's purely visual smoothing — but it's
  // the standard approach for displaying low-res thermal data.
  //
  // Layout: input is 16 cols x 16 rows = 256. Output is 32 cols x 24 rows = 768.
  // We map output coordinates to fractional input coordinates:
  //   ifrac_row = orow * (15.0 / 23.0)   maps 0..23 -> 0.0..15.0
  //   ifrac_col = ocol * (15.0 / 31.0)   maps 0..31 -> 0.0..15.0
  // Then bilinear-interpolate between the 4 surrounding input pixels.
  for (size_t orow = 0; orow < 24; orow++) {
    float ifrac_row = (float)orow * 15.0f / 23.0f;
    size_t irow0 = (size_t)ifrac_row;
    size_t irow1 = (irow0 < 15) ? irow0 + 1 : 15;
    float fry = ifrac_row - (float)irow0;

    for (size_t ocol = 0; ocol < 32; ocol++) {
      float ifrac_col = (float)ocol * 15.0f / 31.0f;
      size_t icol0 = (size_t)ifrac_col;
      size_t icol1 = (icol0 < 15) ? icol0 + 1 : 15;
      float frc = ifrac_col - (float)icol0;

      // Four surrounding input pixels
      float t00 = latestTempsRaw[irow0 * 16 + icol0];
      float t01 = latestTempsRaw[irow0 * 16 + icol1];
      float t10 = latestTempsRaw[irow1 * 16 + icol0];
      float t11 = latestTempsRaw[irow1 * 16 + icol1];

      // Bilinear interpolation
      float t0 = t00 + (t01 - t00) * frc;   // top edge
      float t1 = t10 + (t11 - t10) * frc;   // bottom edge
      float t  = t0  + (t1  - t0)  * fry;   // vertical blend

      latestTemps[orow * 32 + ocol] = t;
    }
  }

  newFrameAvailable = true;

  // Print first few frames for verification, then go quiet
  if (validFrames <= 3) {
    Serial.printf("[uart] frame #%lu  ambient=%.2f C  raw[0]=%.1f  raw[128]=%.1f  raw[255]=%.1f  out[384]=%.1f\n",
                  (unsigned long)validFrames,
                  latestAmbient,
                  latestTempsRaw[0],
                  latestTempsRaw[128],
                  latestTempsRaw[255],
                  latestTemps[384]);
  }
  return true;
}

// ---------------------------------------------------------------------------
// POST the latest frame to the Flask server
// ---------------------------------------------------------------------------
bool postFrame() {
  // ---- Build JSON manually (no StaticJsonDocument — avoids heap fragmentation) ----
  String payload;
  payload.reserve(4600);
  payload += "{\"t\":";
  payload += (uint32_t)millis();
  payload += ",\"ambient\":";
  {
    char abuf[16];
    snprintf(abuf, sizeof(abuf), "%.2f", latestAmbient);
    payload += abuf;
  }
  payload += ",\"temps\":[";
  {
    char buf[16];
    for (size_t i = 0; i < OUTPUT_PIXEL_COUNT; i++) {
      dtostrf(latestTemps[i], 0, 1, buf);
      payload += buf;
      if (i < OUTPUT_PIXEL_COUNT - 1) payload += ',';
    }
  }
  payload += "]}";

  Serial.printf("[http] POSTing %u bytes to %s ...\n",
                (unsigned)payload.length(), SERVER_URL);

  // ---- HTTP POST with bounded timeout ----
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, SERVER_URL)) {
    Serial.println(F("[http] begin() failed"));
    postsFail++;
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);
  if (code > 0) {
    String resp = http.getString();
    Serial.printf("[http] POST ok  code=%d  resp_len=%u\n",
                  code, (unsigned)resp.length());
    postsOk++;
  } else {
    Serial.printf("[http] POST failed  err=%s (%d)\n",
                  http.errorToString(code).c_str(), code);
    postsFail++;
  }
  http.end();
  return true;
}

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------
void connectWiFi() {
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.setTxPower((wifi_power_t)WIFI_TX_POWER_DBM);
  Serial.printf(" (TX=%d dBm)\n", WIFI_TX_POWER_DBM);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000) {
    delay(300);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\n[wifi] FAILED (30 s timeout). Will retry next loop."));
    return;
  }

  Serial.printf("\n[wifi] connected. IP=%s  RSSI=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  delay(200);
}

// ---------------------------------------------------------------------------
// Optional: send a command to the GY-MCU90640 (e.g. change refresh rate)
// ---------------------------------------------------------------------------
// Most GY-MCU90640 firmware uses a single-byte command sent to the module's RX:
//   0xA5 0x45 0x01  -> set refresh rate to 1 Hz
//   0xA5 0x45 0x02  -> set refresh rate to 2 Hz
//   0xA5 0x45 0x03  -> set refresh rate to 4 Hz (default)
//   0xA5 0x45 0x04  -> set refresh rate to 8 Hz
// Commands are followed by a checksum byte: 0xFF - (sum of command bytes) + 1
// This is firmware-dependent — check your module's documentation.
// Uncomment and call from setup() if you need to change the refresh rate.
void sendConfigCommand(uint8_t cmd) {
  Serial2.write(0xA5);
  Serial2.write(0x45);
  Serial2.write(cmd);
  Serial2.write((uint8_t)(0xFF - (0xA5 + 0x45 + cmd) + 1));
  Serial.printf("[uart] sent config command: refresh rate 0x%02X\n", cmd);
}
