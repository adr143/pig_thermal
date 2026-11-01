# Pig Monitoring System

Windows-first monitoring dashboard using an ESP32 thermal camera, a Flask + Socket.IO backend, and a Next.js web app. Linux setup notes and Bash equivalents are included for future deployments.

## Requirements

- Windows 10/11 with PowerShell
- Python 3.10+ (Python 3.11 recommended)
- Node.js 18+ and npm
- ESP32 Arduino toolchain and the libraries required by the sketch

The backend uses Flask-SocketIO's cross-platform `threading` mode. It does not use Eventlet.

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

Flash `arduino\esp32_thermal_cam\esp32_thermal_cam.ino` after copying `arduino\esp32_thermal_cam\secrets.example.h` to `arduino\esp32_thermal_cam\secrets.h` and setting your Wi-Fi credentials and backend address. The local `secrets.h` is ignored by Git.

Start the backend and web app in separate PowerShell windows:

```powershell
.\scripts\run_backend.ps1
```

```powershell
.\scripts\run_web.ps1
```

Open [http://localhost:3000](http://localhost:3000). The Next.js development server proxies API and WebSocket requests to Flask.

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

- `arduino/esp32_thermal_cam/` — ESP32 sketch and Wi-Fi/server configuration template.
- `server/` — Flask API, Socket.IO namespaces, inference, and snapshot scheduling.
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