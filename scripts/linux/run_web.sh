#!/usr/bin/env bash
# Start the Next.js dev server.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
WEB_DIR="$PROJECT_DIR/web"

if [ ! -d "$WEB_DIR/node_modules" ]; then
    echo "[run] node_modules not found. Run bash scripts/linux/setup.sh first."
    exit 1
fi

cd "$WEB_DIR"
exec npm run dev
