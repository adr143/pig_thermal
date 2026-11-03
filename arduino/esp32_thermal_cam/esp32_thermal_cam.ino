#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include "secrets.h"

#ifndef WS_HOST
  #define WS_HOST           "192.168.0.103"
#endif
#define SERVER_URL          "http://" WS_HOST ":5000/thermal"

#ifndef UART_RX_PIN
  #define UART_RX_PIN       16
#endif
#ifndef UART_TX_PIN
  #define UART_TX_PIN       17

#endif
#ifndef UART_BAUD
  #define UART_BAUD         115200
#endif
#ifndef RX_BUFFER_SIZE

#define RX_BUFFER_SIZE    4096
#endif

#ifndef PS_PIN
  #define PS_PIN            4
#endif

#ifndef WIFI_TX_POWER_DBM
  #define WIFI_TX_POWER_DBM   17
#endif

#define FRAME_INTERVAL_MS     5000
#define HTTP_TIMEOUT_MS       4000
#define NO_FRAME_WARN_MS      5000
#define NO_FRAME_RECOVER_MS   15000

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

#define HEADER_BYTE_3      0x06
#endif

#ifndef MLX_BYTE_ORDER_LE
  #define MLX_BYTE_ORDER_LE   1
#endif

#define RAW_PIXEL_COUNT       256
#define OUTPUT_PIXEL_COUNT    768

#define HEADER_LEN           4
#define PIXEL_BYTES          (RAW_PIXEL_COUNT * 2)
#define TRAILER_LEN          1
#define FRAME_LEN            (HEADER_LEN + PIXEL_BYTES + TRAILER_LEN)

static const uint8_t HEADER[HEADER_LEN] = { HEADER_BYTE_0, HEADER_BYTE_1,
                                            HEADER_BYTE_2, HEADER_BYTE_3 };

static float      latestTemps[OUTPUT_PIXEL_COUNT];
static float      latestTempsRaw[RAW_PIXEL_COUNT];
static float      emaBuffer[RAW_PIXEL_COUNT];
static bool       emaInitialized = false;
static float      blurBuffer[RAW_PIXEL_COUNT];
static float      latestAmbient   = 0.0f;
static bool       newFrameAvailable = false;
static uint32_t   lastFrameReceivedMs = 0;

enum ParseState { SYNC, READ_BODY };
ParseState  parseState = SYNC;
uint8_t     rxFrame[FRAME_LEN];
size_t      rxLen = 0;

uint32_t    lastSendMs        = 0;
uint32_t    validFrames       = 0;
uint32_t    checksumErrors    = 0;
uint32_t    postsOk           = 0;
uint32_t    postsFail         = 0;
uint32_t    uartRestarts      = 0;

void connectWiFi();
void parseUart();
bool processFrame();
bool postFrame();
void restartUartReceiver();
void sendConfigCommand(uint8_t cmd);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("[boot] ESP32 Thermal Node (HTTP POST, UART mode)"));

#if PS_PIN >= 0
    pinMode(PS_PIN, OUTPUT);
    digitalWrite(PS_PIN, HIGH);
    Serial.printf("[boot] PS pin GPIO%d -> HIGH (UART mode)\n", PS_PIN);
    delay(50);
  #else
    Serial.println(F("[boot] PS_PIN disabled — verify PS is HIGH on the board"));
  #endif

Serial2.setRxBufferSize(RX_BUFFER_SIZE);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  lastFrameReceivedMs = millis();
  Serial.printf("[boot] UART2 ready  RX=GPIO%d  TX=GPIO%d  baud=%u  rx_buf=%u\n",
                UART_RX_PIN, UART_TX_PIN, UART_BAUD, RX_BUFFER_SIZE);

Serial.println(F("[boot] waiting for GY-MCU90640 to start streaming..."));

  connectWiFi();
}

void loop() {

  parseUart();

if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[wifi] disconnected, reconnecting..."));
    connectWiFi();
    return;
  }

if (newFrameAvailable && (millis() - lastSendMs >= FRAME_INTERVAL_MS)) {
    lastSendMs = millis();
    newFrameAvailable = false;
    postFrame();
  }

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

void parseUart() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read();

    switch (parseState) {
      case SYNC: {
        rxFrame[rxLen++] = b;
        if (rxLen >= HEADER_LEN) {

          size_t off = rxLen - HEADER_LEN;
          if (memcmp(rxFrame + off, HEADER, HEADER_LEN) == 0) {

            if (off != 0) {
              memmove(rxFrame, rxFrame + off, HEADER_LEN);
            }
            rxLen = HEADER_LEN;
            parseState = READ_BODY;
          } else {

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

        if (rxLen > FRAME_LEN) {
          rxLen = 0;
          parseState = SYNC;
        }
        break;
      }
    }
  }
}

bool processFrame() {

size_t sensible = 0;
  for (size_t i = 0; i < RAW_PIXEL_COUNT; i++) {
    size_t off = HEADER_LEN + i * 2;
    int16_t raw = (int16_t)((rxFrame[off + 1] << 8) | rxFrame[off]);
    float t = raw / 100.0f;
    if (t >= -20.0f && t <= 200.0f) sensible++;
  }
  if (sensible < RAW_PIXEL_COUNT * 8 / 10) {
    checksumErrors++;
    if (checksumErrors <= 10 || (checksumErrors % 100) == 0) {
      Serial.printf("[uart] sanity check failed: %u/%u pixels in range — desync, rejecting\n",
                    (unsigned)sensible, RAW_PIXEL_COUNT);
    }
    return false;
  }

  validFrames++;
  lastFrameReceivedMs = millis();

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
  latestAmbient = latestTempsRaw[0];

{
    const float alpha = 0.35f;
    if (!emaInitialized) {

      for (size_t i = 0; i < RAW_PIXEL_COUNT; i++) emaBuffer[i] = latestTempsRaw[i];
      emaInitialized = true;
    } else {
      for (size_t i = 0; i < RAW_PIXEL_COUNT; i++) {
        emaBuffer[i] = emaBuffer[i] + alpha * (latestTempsRaw[i] - emaBuffer[i]);
        latestTempsRaw[i] = emaBuffer[i];
      }
    }
  }

{

    float gSum = 0.0f;
    for (size_t i = 1; i < RAW_PIXEL_COUNT; i++) gSum += latestTempsRaw[i];
    float gMean = gSum / (RAW_PIXEL_COUNT - 1);

    float gVarSum = 0.0f;
    for (size_t i = 1; i < RAW_PIXEL_COUNT; i++) {
      float d = latestTempsRaw[i] - gMean;
      gVarSum += d * d;
    }
    float gStd = sqrtf(gVarSum / (RAW_PIXEL_COUNT - 1));
    if (gStd < 0.1f) gStd = 0.1f;

for (size_t row = 0; row < 16; row++) {

      float rSum = 0.0f;
      int rCount = 0;
      for (size_t col = 0; col < 16; col++) {
        size_t idx = row * 16 + col;
        if (idx > 0) { rSum += latestTempsRaw[idx]; rCount++; }
      }
      float rMean = rSum / rCount;

float rVarSum = 0.0f;
      for (size_t col = 0; col < 16; col++) {
        size_t idx = row * 16 + col;
        if (idx > 0) {
          float d = latestTempsRaw[idx] - rMean;
          rVarSum += d * d;
        }
      }
      float rStd = sqrtf(rVarSum / rCount);
      if (rStd < 0.1f) rStd = 0.1f;

float gain = gStd / rStd;
      for (size_t col = 0; col < 16; col++) {
        size_t idx = row * 16 + col;
        if (idx > 0) {
          latestTempsRaw[idx] = (latestTempsRaw[idx] - rMean) * gain + gMean;
        }
      }
    }
  }

{

    memcpy(blurBuffer, latestTempsRaw, sizeof(blurBuffer));

    for (size_t row = 0; row < 16; row++) {
      for (size_t col = 0; col < 16; col++) {
        size_t idx = row * 16 + col;
        if (idx == 0) continue;

size_t rowU = (row > 0) ? row - 1 : 0;
        size_t rowD = (row < 15) ? row + 1 : 15;
        size_t colL = (col > 0) ? col - 1 : 0;
        size_t colR = (col < 15) ? col + 1 : 15;

float pUL = blurBuffer[rowU * 16 + colL];
        float pUC = blurBuffer[rowU * 16 + col];
        float pUR = blurBuffer[rowU * 16 + colR];
        float pML = blurBuffer[row * 16 + colL];
        float pMC = blurBuffer[row * 16 + col];
        float pMR = blurBuffer[row * 16 + colR];
        float pDL = blurBuffer[rowD * 16 + colL];
        float pDC = blurBuffer[rowD * 16 + col];
        float pDR = blurBuffer[rowD * 16 + colR];

latestTempsRaw[idx] = (pUL + 2*pUC + pUR +
                               2*pML + 4*pMC + 2*pMR +
                               pDL + 2*pDC + pDR) / 16.0f;
      }
    }
  }

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

float t00 = latestTempsRaw[irow0 * 16 + icol0];
      float t01 = latestTempsRaw[irow0 * 16 + icol1];
      float t10 = latestTempsRaw[irow1 * 16 + icol0];
      float t11 = latestTempsRaw[irow1 * 16 + icol1];

float t0 = t00 + (t01 - t00) * frc;
      float t1 = t10 + (t11 - t10) * frc;
      float t  = t0  + (t1  - t0)  * fry;

      latestTemps[orow * 32 + ocol] = t;
    }
  }

  newFrameAvailable = true;

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

bool postFrame() {

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

void sendConfigCommand(uint8_t cmd) {
  Serial2.write(0xA5);
  Serial2.write(0x45);
  Serial2.write(cmd);
  Serial2.write((uint8_t)(0xFF - (0xA5 + 0x45 + cmd) + 1));
  Serial.printf("[uart] sent config command: refresh rate 0x%02X\n", cmd);
}
