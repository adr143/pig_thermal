set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
SERVER_DIR="$PROJECT_DIR/server"

if [ ! -d "$SERVER_DIR/.venv" ]; then
    echo "[run] venv not found. Run bash scripts/linux/setup.sh first."
    exit 1
fi

source "$SERVER_DIR/.venv/bin/activate"
cd "$SERVER_DIR"
exec python app.py
