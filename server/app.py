"""
Flask app entry point.

  - Defines routes (REST API)
  - Serves raw WebSocket endpoints for ESP32 thermal input and browser streams
  - Starts webcam thread + scheduler on boot
  - Serves the snapshot JPEGs from disk

Run:
    cd server
    python app.py

Default port: 5000
"""
from __future__ import annotations

import logging
import os
import threading
from datetime import datetime

import db
import snapshot
import thermal_ws_server
import stream_ws_server
from config import config
from flask import Flask, jsonify, send_from_directory, request
from flask_sock import Sock
from live_state import state
from webcam import webcam_loop

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("app")

# ---------------------------------------------------------------------------
# Flask + raw WebSockets
# ---------------------------------------------------------------------------
app = Flask(__name__, static_folder=None)
app.config["SECRET_KEY"] = os.getenv("SECRET_KEY", "change-me-in-prod")
app.config["CORS_HEADERS"] = "Content-Type"
app.config["SOCK_SERVER_OPTIONS"] = {
    "ping_interval": 25,
    "max_message_size": 1024 * 1024,
}
sock = Sock(app)
thermal_ws_server.register_thermal_route(sock)
stream_ws_server.register_stream_route(sock)

# ---------------------------------------------------------------------------
# Background threads
# ---------------------------------------------------------------------------
_stop_event = threading.Event()
_webcam_thread = None


def _start_background() -> None:
    global _webcam_thread
    db.init_db()
    snapshot.init_scheduler()
    if _webcam_thread is None or not _webcam_thread.is_alive():
        _webcam_thread = threading.Thread(
            target=webcam_loop, args=(_stop_event,), daemon=True
        )
        _webcam_thread.start()
        log.info("webcam thread started")


# ---------------------------------------------------------------------------
# Routes: health + status
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    return jsonify({
        "service": "pig-monitoring-backend",
        "ok": True,
        "thermal_clients": state.thermal_clients,
        "browser_clients": state.browser_clients,
        "thermal_age_s": round(state.thermal_age_s(), 1),
    })


@app.route("/api/status")
def api_status():
    from inference import models_loaded
    jpeg, dets, stats = state.get_webcam()
    return jsonify({
        "models_loaded": models_loaded(),
        "thermal_clients": state.thermal_clients,
        "browser_clients": state.browser_clients,
        "thermal_age_s": round(state.thermal_age_s(), 1),
        "webcam_pig_count": stats.pig_count,
        "webcam_classifications": stats.classifications,
        "avg_thermal_c": stats.avg_thermal_c,
    })


# ---------------------------------------------------------------------------
# Routes: snapshots
# ---------------------------------------------------------------------------
@app.route("/api/snapshots", methods=["GET"])
def api_list_snapshots():
    limit = min(int(request.args.get("limit", 100)), 500)
    offset = max(int(request.args.get("offset", 0)), 0)
    return jsonify(db.list_snapshots(limit=limit, offset=offset))


@app.route("/api/snapshots/<int:sid>", methods=["GET"])
def api_get_snapshot(sid):
    snap = db.get_snapshot(sid)
    if snap is None:
        return jsonify({"error": "not_found"}), 404
    return jsonify(snap)


@app.route("/api/snapshots/<int:sid>", methods=["DELETE"])
def api_delete_snapshot(sid):
    ok = db.delete_snapshot(sid)
    if not ok:
        return jsonify({"error": "not_found"}), 404
    return jsonify({"ok": True, "deleted": sid})


@app.route("/api/snapshots/<int:sid>/image", methods=["GET"])
def api_snapshot_image(sid):
    snap = db.get_snapshot(sid)
    if snap is None or not snap.get("snapshot_path"):
        return jsonify({"error": "not_found"}), 404
    directory = os.path.dirname(os.path.abspath(snap["snapshot_path"])) or "."
    fname = os.path.basename(snap["snapshot_path"])
    full = os.path.abspath(snap["snapshot_path"])
    if not os.path.isfile(full):
        return jsonify({"error": "file_missing"}), 404
    return send_from_directory(directory, fname)


@app.route("/api/snapshots/take", methods=["POST"])
def api_take_snapshot():
    result = snapshot.trigger_manual_snapshot()
    code = 200 if result.get("ok") else 503
    return jsonify(result), code


@app.route("/api/snapshots/<int:sid>/pigs/<int:pig_id>", methods=["PUT"])
def api_update_pig_temp(sid, pig_id):
    body = request.get_json(silent=True) or {}
    temp = body.get("temperature_c")
    if temp is None:
        return jsonify({"error": "temperature_c required"}), 400
    try:
        temp = float(temp)
    except (TypeError, ValueError):
        return jsonify({"error": "temperature_c must be numeric"}), 400
    if not db.update_pig_temperature(pig_id, temp):
        return jsonify({"error": "not_found"}), 404
    return jsonify({"ok": True, "pig_id": pig_id, "temperature_c": temp})


# ---------------------------------------------------------------------------
# Routes: schedule
# ---------------------------------------------------------------------------
@app.route("/api/schedule", methods=["GET"])
def api_get_schedule():
    return jsonify(snapshot.get_schedule())


@app.route("/api/schedule", methods=["PUT"])
def api_update_schedule():
    body = request.get_json(silent=True) or {}
    enabled = body.get("enabled")
    if enabled is not None:
        enabled = bool(enabled)
    mode = body.get("mode")
    if mode is not None and mode not in ("interval", "cron"):
        return jsonify({"error": "mode must be 'interval' or 'cron'"}), 400
    interval_min = body.get("interval_min")
    if interval_min is not None:
        interval_min = int(interval_min)
        if interval_min < 1:
            return jsonify({"error": "interval_min must be >= 1"}), 400
    cron_expr = body.get("cron_expr")
    new_state = snapshot.update_schedule(
        enabled=enabled, mode=mode,
        interval_min=interval_min, cron_expr=cron_expr,
    )
    return jsonify(new_state)


# ---------------------------------------------------------------------------
# Routes: thermal config
# ---------------------------------------------------------------------------
@app.route("/api/config/thermal", methods=["GET"])
def api_get_thermal_config():
    return jsonify({
        "pig_threshold_c": config.THERMAL_PIG_THRESHOLD_C,
        "min_hot_fraction": config.THERMAL_MIN_HOT_FRACTION,
        "width": config.THERMAL_WIDTH,
        "height": config.THERMAL_HEIGHT,
    })


# ---------------------------------------------------------------------------
# Routes: live single-frame fetch (HTTP fallback)
# ---------------------------------------------------------------------------
@app.route("/api/live/webcam.jpg")
def api_live_webcam_jpg():
    jpeg, _, _ = state.get_webcam()
    if not jpeg:
        return ("no frame", 503)
    from flask import Response
    return Response(jpeg, mimetype="image/jpeg")


# ---------------------------------------------------------------------------
# Boot
# ---------------------------------------------------------------------------
@app.before_request
def _boot_check():
    """Lazy-init background threads on first request."""
    if not getattr(app, "_inited", False):
        app._inited = True
        _start_background()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def _run_dev():
    """Run the Flask-Sock app with Werkzeug's raw WebSocket support."""
    log.info("starting Pig Monitoring backend (dev) on %s:%d",
             config.HOST, config.PORT)
    _start_background()
    try:
        app.run(
            host=config.HOST,
            port=config.PORT,
            debug=config.DEBUG,
            use_reloader=False,
            threaded=True,
        )
    finally:
        _stop_event.set()
        snapshot.shutdown_scheduler()


def _run_prod():
    """Run with Flask debug mode disabled."""
    log.info("starting Pig Monitoring backend on %s:%d",
             config.HOST, config.PORT)
    _start_background()
    try:
        app.run(
            host=config.HOST,
            port=config.PORT,
            debug=False,
            use_reloader=False,
            threaded=True,
        )
    finally:
        _stop_event.set()
        snapshot.shutdown_scheduler()


if __name__ == "__main__":
    if os.getenv("PIGMON_PROD", "0") == "1":
        _run_prod()
    else:
        _run_dev()
