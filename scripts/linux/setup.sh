set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SERVER_DIR="$PROJECT_DIR/server"
WEB_DIR="$PROJECT_DIR/web"

echo "[setup] project: $PROJECT_DIR"

if ! command -v python3 >/dev/null 2>&1; then
    echo "[setup] python3 not found. Install Python 3.10+ first."
    exit 1
fi
echo "[setup] creating python venv at $SERVER_DIR/.venv"
python3 -m venv "$SERVER_DIR/.venv"
source "$SERVER_DIR/.venv/bin/activate"
echo "[setup] installing python deps (this may take a few minutes)"
pip install --upgrade pip
pip install -r "$SERVER_DIR/requirements.txt"

if [ ! -f "$SERVER_DIR/.env" ]; then
    echo "[setup] copying server/.env.example -> server/.env"
    cp "$SERVER_DIR/.env.example" "$SERVER_DIR/.env"
fi

mkdir -p "$SERVER_DIR/snapshots"
mkdir -p "$SERVER_DIR/models"

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
echo "  4. Start the backend:  bash scripts/linux/run_backend.sh"
echo "  5. Start the UI:      bash scripts/linux/run_web.sh"
