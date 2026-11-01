# Pig Monitoring System

Pig monitoring system with an ESP32 thermal camera, a Flask + Socket.IO backend, and a Next.js dashboard.

## Project layout

- `arduino/esp32_thermal_cam/` — ESP32 sketch and Wi-Fi/server configuration template.
- `server/` — Flask API, streaming services, inference, and snapshot scheduling.
- `server/models/` — Place `pig_detect.pt` and `pig_classify.pt` here.
- `web/` — Next.js dashboard.
- `scripts/` — Setup and run scripts.

See each component's configuration and scripts for setup details.
