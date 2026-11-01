#!/usr/bin/env bash
# systemd service file generator for the Flask backend.
# Run as root: sudo ./scripts/install_service.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER_DIR="$PROJECT_DIR/server"

# Detect the user that owns the project dir
OWNER_USER="$(stat -c '%U' "$PROJECT_DIR")"

SERVICE_FILE="/etc/systemd/system/pigmonitor.service"

cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Pig Monitoring System Backend (Flask + Socket.IO)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$OWNER_USER
WorkingDirectory=$SERVER_DIR
EnvironmentFile=$SERVER_DIR/.env
ExecStart=$SERVER_DIR/.venv/bin/python app.py
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

echo "[install] wrote $SERVICE_FILE"
echo "[install] enabling + starting service..."
systemctl daemon-reload
systemctl enable pigmonitor.service
systemctl restart pigmonitor.service
echo
echo "Check status:  sudo systemctl status pigmonitor"
echo "View logs:      sudo journalctl -u pigmonitor -f"
