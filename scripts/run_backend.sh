#!/usr/bin/env bash
# Start the Flask backend (uses venv created by setup.sh).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_DIR="$PROJECT_DIR/server"

if [ ! -d "$SERVER_DIR/.venv" ]; then
    echo "[run] venv not found. Run ./scripts/setup.sh first."
    exit 1
fi

# shellcheck disable=SC1091
source "$SERVER_DIR/.venv/bin/activate"
cd "$SERVER_DIR"
exec python app.py
