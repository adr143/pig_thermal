# Pig Monitoring System

Windows-first monitoring dashboard using an ESP32 thermal camera, a Flask backend with raw WebSocket endpoints, and a Next.js web app. Linux setup notes and Bash equivalents are included for future deployments.

## Requirements

- Windows 10/11 with PowerShell
- Python 3.10+ (Python 3.11 recommended)
- Node.js 18+ and npm
- ESP32 Arduino toolchain and the libraries required by the sketch

Thermal data and browser live updates use standard raw WebSockets; the backend does not require the Socket.IO protocol, Eventlet, or a separate greenlet-based server.

## Setup on Windows

From the project root, run PowerShell. If script execution is blocked, enable local scripts for your user:

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

Install the Python and web dependencies, and create local environment files:

```powershell
.\scripts\setup.ps1
```

The setup creates `server\.venv`, installs `server\requirements.txt` and `web` npm dependencies, and copies the example environment files when local ones do not already exist.

Place `pig_detect.pt` and `pig_classify.pt` in `server\models\`. Review `server\.env` and `web\.env.local` if you need to change defaults. The default backend port is 5000 and the web app port is 3000.

Copy `arduino\esp32_thermal_cam\secrets.example.h` to `arduino\esp32_thermal_cam\secrets.h`, set the Wi-Fi credentials, and set `WS_HOST` to the mini-PC's LAN IP (for example `192.168.0.103`). Flash `arduino\esp32_thermal_cam\esp32_thermal_cam.ino`; it sends thermal frames over Wi-Fi to the backend WebSocket at `/ws/thermal`.

Start the backend and web app in separate PowerShell windows:

```powershell
.\scripts\run_backend.ps1
```

```powershell
.\scripts\run_web.ps1
```

Open [http://localhost:3000](http://localhost:3000). Next.js proxies HTTP API requests to Flask. The browser connects directly to Flask at `ws://<page-host>:5000/ws/stream`, while the ESP32 sends thermal frames to `ws://<mini-PC-IP>:5000/ws/thermal`. The WebSocket endpoints accept connections from any origin. Set `NEXT_PUBLIC_WS_BASE` in `web\.env.local` if Flask is on another host or accessed through TLS.

The Windows backend launcher binds to `0.0.0.0:5000` so other devices on the LAN can reach it. To allow inbound connections through Windows Defender Firewall on a **Private** network, open an Administrator PowerShell at the project root and run:

```powershell
.\scripts\allow_backend_firewall.ps1
```

Restart the backend with `.\scripts\run_backend.ps1`, then check `http://<mini-PC-IP>:5000/` from another device (for example `http://192.168.0.103:5000/`). It should return backend status JSON. The dashboard WebSocket is `ws://<mini-PC-IP>:5000/ws/stream`. If it still reports `Invalid frame header`, verify the status URL returns JSON from the mini-PC and that the backend console shows `starting Pig Monitoring backend (dev) on 0.0.0.0:5000`; those checks confirm whether the client is reaching this Flask process.

The ESP32 sends each thermal frame to `/ws/thermal` as a raw WebSocket text message containing JSON: `{"type":"frame","t":1234,"ambient":24.5,"temps":[...]}`. The backend validates and broadcasts it to browsers over `/ws/stream` using raw WebSocket JSON messages with a top-level `type` such as `thermal_frame`, `webcam_frame`, or `snapshot_taken`.

To start the apps automatically at user logon:

```powershell
.\scripts\install_autostart.ps1
```

For system startup instead of user logon, run PowerShell as Administrator and use `.\scripts\install_autostart.ps1 -AtBoot`. Remove the scheduled tasks with `.\scripts\uninstall_autostart.ps1`.

## Linux notes

The Linux scripts are kept under `scripts/linux/`. From the project root, run:

```bash
bash scripts/linux/setup.sh
bash scripts/linux/run_backend.sh
bash scripts/linux/run_web.sh
```

The backend systemd installer is `sudo bash scripts/linux/install_service.sh`. It installs only the backend service; run the web app separately.

## Project layout

- `arduino/esp32_thermal_cam/` — ESP32 thermal-camera sketch and Wi-Fi/server configuration template.
- `server/` — Flask API, raw WebSocket endpoints, inference, and snapshot scheduling.
- `server/models/` — Place the trained model files here.
- `server/snapshots/` and `server/pigs.db` — Created at runtime.
- `web/` — Next.js dashboard.
- `scripts/` — Windows PowerShell setup, run, and autostart scripts.
- `scripts/linux/` — Linux Bash equivalents.

# One-time: allow PS scripts
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned

# Setup (creates venv, installs Python + npm deps)
cd C:\path\to\pig-monitoring-system
.\scripts\setup.ps1

# Drop your models in
Copy-Item C:\path\to\pig_detect.pt     server\models\pig_detect.pt
Copy-Item C:\path\to\pig_classify.pt   server\models\pig_classify.pt

# Start backend (Terminal 1)
.\scripts\run_backend.ps1

# Start web UI (Terminal 2)
.\scripts\run_web.ps1

# Open http://localhost:3000