from __future__ import annotations

import json
import logging
import math
import threading
import time
from typing import Any, List

import numpy as np
from flask import Flask, request
from flask_sock import Sock
from simple_websocket import ConnectionClosed

from config import config
from live_state import ThermalFrame, state
from websocket_hub import broadcast, send_event

log = logging.getLogger("thermal")
_last_broadcast_ms = 0
_broadcast_lock = threading.Lock()
_received_frame_count = 0
_received_frame_count_lock = threading.Lock()


def register_thermal_http_route(app: Flask) -> None:
    @app.route("/thermal", methods=["POST"])
    def thermal_http_post():
        data = request.get_json(silent=True)
        if not isinstance(data, dict):
            return {"error": "expected a JSON object"}, 400


        data.setdefault("type", "frame")
        if not handle_thermal_message(data, source="HTTP POST"):
            return {"error": "invalid thermal frame"}, 400
        return {"ok": True}, 200


def register_thermal_route(sock: Sock) -> None:
    @sock.route("/ws/thermal")
    def thermal_socket(ws: Any) -> None:
        clients = state.inc_thermal_client()
        log.info("thermal: ESP32 connected from %s (clients=%d)",
                 request.remote_addr, clients)
        try:
            ws.send(send_event("ready", {"msg": "send frames"}))
            while True:
                try:
                    message = ws.receive()
                except ConnectionClosed:
                    break
                if message is None:
                    break
                handle_thermal_message(message, source="websocket")
        except Exception:
            log.exception("thermal: WebSocket connection failed")
        finally:
            clients = state.dec_thermal_client()
            log.info("thermal: ESP32 disconnected (clients=%d)", clients)


def handle_thermal_message(message: Any, source: str = "websocket") -> bool:
    if isinstance(message, bytes):
        try:
            message = message.decode("utf-8")
        except UnicodeDecodeError:
            log.warning("thermal: received non-UTF-8 binary frame")
            return False
    if not isinstance(message, str):
        if isinstance(message, dict):
            data = message
        else:
            log.warning("thermal: received unsupported message type")
            return False
    else:
        try:
            data = json.loads(message)
        except json.JSONDecodeError:
            log.warning("thermal: received invalid JSON")
            return False

    if not isinstance(data, dict):
        log.warning("thermal: expected a JSON object")
        return False

    message_type = data.get("type")
    if message_type == "heartbeat":
        log.debug("thermal: heartbeat from ESP32")
        return True
    if message_type != "frame":
        log.debug("thermal: ignoring message type %r", message_type)
        return False

    temps = data.get("temps")
    expected_count = config.THERMAL_WIDTH * config.THERMAL_HEIGHT
    if not isinstance(temps, list) or len(temps) != expected_count:
        log.warning("thermal: bad temps length=%s (expected %d)",
                    len(temps) if isinstance(temps, list) else None,
                    expected_count)
        return False
    try:
        temps_f: List[float] = [float(temp) for temp in temps]
        ambient = float(data.get("ambient", 0.0))
    except (TypeError, ValueError):
        log.warning("thermal: non-numeric temperature received")
        return False
    if not math.isfinite(ambient) or not all(math.isfinite(temp) for temp in temps_f):
        log.warning("thermal: non-finite temperature received")
        return False

    received_at = time.time()
    state.set_thermal(ThermalFrame(
        temps=temps_f,
        ambient=ambient,
        received_at=received_at,
    ))
    _log_frame_received(temps_f, ambient, source)
    _broadcast_to_browsers(temps_f, ambient, received_at)
    return True


def _log_frame_received(temps: List[float], ambient: float, source: str) -> None:
    global _received_frame_count
    with _received_frame_count_lock:
        _received_frame_count += 1
        frame_count = _received_frame_count
    if frame_count == 1 or frame_count % 20 == 0:
        log.info(
            "thermal: received %s frame #%d ambient=%.1fC "
            "range=%.1f-%.1fC first_pixels=%s",
            source,
            frame_count,
            ambient,
            min(temps),
            max(temps),
            ",".join(f"{temp:.1f}" for temp in temps[:5]),
        )


def _broadcast_to_browsers(temps: List[float], ambient: float, received_at: float) -> None:
    global _last_broadcast_ms
    now_ms = int(received_at * 1000)
    with _broadcast_lock:
        if now_ms - _last_broadcast_ms < config.THERMAL_BROADCAST_MIN_MS:
            return
        _last_broadcast_ms = now_ms

    arr = np.asarray(temps, dtype=np.float32)
    hot = arr[arr >= config.THERMAL_PIG_THRESHOLD_C]
    if hot.size / arr.size >= config.THERMAL_MIN_HOT_FRACTION:
        avg_hot = float(hot.mean())
    else:
        avg_hot = None

    broadcast("thermal_frame", {
        "t": now_ms,
        "ambient": round(ambient, 2),
        "temps": temps,
        "stats": {
            "avg_all_c": round(float(arr.mean()), 2),
            "avg_hot_c": round(avg_hot, 2) if avg_hot is not None else None,
            "max_c": round(float(arr.max()), 2),
            "min_c": round(float(arr.min()), 2),
        },
    })
