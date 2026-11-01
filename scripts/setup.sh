#!/usr/bin/env bash
# ============================================================
#  Pig Monitoring System — Setup script
#  Run once on the mini-PC.
# ============================================================
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_DIR="$PROJECT_DIR/server"
WEB_DIR="$PROJECT_DIR/web"

echo "[setup] project: $PROJECT_DIR"

# 1. Python venv
if ! command -v python3 >/dev/null 2>&1; then
    echo "[setup] python3 not found. Install Python 3.10+ first."
    exit 1
fi
echo "[setup] creating python venv at $SERVER_DIR/.venv"
python3 -m venv "$SERVER_DIR/.venv"
# shellcheck disable=SC1091
source "$SERVER_DIR/.venv/bin/activate"
echo "[setup] installing python deps (this may take a few minutes)"
pip install --upgrade pip
pip install -r "$SERVER_DIR/requirements.txt"

# 2. Env file
if [ ! -f "$SERVER_DIR/.env" ]; then
    echo "[setup] copying server/.env.example -> server/.env"
    cp "$SERVER_DIR/.env.example" "$SERVER_DIR/.env"
fi

# 3. Snapshots directory
mkdir -p "$SERVER_DIR/snapshots"
mkdir -p "$SERVER_DIR/models"

# 4. Node deps
if command -v npm >/dev/null 2>&1; then
    echo "[setup] installing next.js deps"
    (cd "$WEB_DIR" && npm install)
else
    echo "[setup] npm not found. Install Node.js 18+ and run: cd web && npm install"
fi

if [ ! -f "$WEB_DIR/.env.local" ]; then
    echo "[setup] copying web/.env.example -> web/.env.local"
    cp "$WEB_DIR/.env.example" "$WEB_DIR/.env.local"
fi

echo "[setup] done."
echo
echo "Next steps:"
echo "  1. Drop your .pt models into server/models/ (pig_detect.pt, pig_classify.pt)"
echo "  2. Edit server/.env and set WS_HOST etc. (or use defaults)"
echo "  3. Flash the ESP32 sketch from arduino/esp32_thermal_cam/"
echo "  4. Start the backend:  ./scripts/run_backend.sh"
echo "  5. Start the UI:      ./scripts/run_web.sh"
