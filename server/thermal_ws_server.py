"""
Thermal stream handler.

Receives 32x24 MLX90640 frames from the ESP32 over a dedicated WebSocket
namespace '/ws/thermal'. Re-broadcasts to browser clients on '/ws/stream'
as 'thermal_frame' events with computed average temperature.
"""
from __future__ import annotations

import logging
import time
from typing import Any, Dict, List

import numpy as np
from flask import request
from flask_socketio import Namespace, emit

from config import config
from live_state import ThermalFrame, state

log = logging.getLogger("thermal")


class ThermalNamespace(Namespace):
    """ESP32 connects here as a WS client and emits 'frame' messages."""

    def on_connect(self, *args, **kwargs):
        state.inc_thermal_client()
        log.info("thermal: ESP32 connected from %s (clients=%d)",
                 request.remote_addr, state.thermal_clients)
        emit("ready", {"msg": "send frames"})

    def on_disconnect(self, *args, **kwargs):
        state.dec_thermal_client()
        log.info("thermal: ESP32 disconnected (clients=%d)", state.thermal_clients)

    def on_frame(self, data: Any):
        """ESP32 sends {"type":"frame","t":<ms>,"ambient":<float>,"temps":[768 floats]}."""
        if isinstance(data, str):
            # Some clients send text; try not to crash but ignore
            log.warning("thermal: received string frame, ignoring")
            return
        if not isinstance(data, dict):
            return
        temps = data.get("temps")
        if not isinstance(temps, list) or len(temps) != config.THERMAL_WIDTH * config.THERMAL_HEIGHT:
            log.warning("thermal: bad temps length=%s (expected %d)",
                        len(temps) if isinstance(temps, list) else None,
                        config.THERMAL_WIDTH * config.THERMAL_HEIGHT)
            return
        try:
            temps_f: List[float] = [float(t) for t in temps]
        except (TypeError, ValueError):
            log.warning("thermal: non-numeric temps received")
            return

        ambient = float(data.get("ambient", 0.0))
        t_recv = time.time()
        tf = ThermalFrame(temps=temps_f, ambient=ambient, received_at=t_recv)
        state.set_thermal(tf)

        # Broadcast to browser clients (throttled)
        self._broadcast_to_browsers(temps_f, ambient, t_recv)

    def on_heartbeat(self, data):
        log.debug("thermal: heartbeat from ESP32: %s", data)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    _last_broadcast_ms = 0

    def _broadcast_to_browsers(self, temps: List[float], ambient: float, t: float):
        now_ms = int(t * 1000)
        if now_ms - ThermalNamespace._last_broadcast_ms < config.THERMAL_BROADCAST_MIN_MS:
            return
        ThermalNamespace._last_broadcast_ms = now_ms

        arr = np.asarray(temps, dtype=np.float32)
        hot = arr[arr >= config.THERMAL_PIG_THRESHOLD_C]
        if hot.size / arr.size >= config.THERMAL_MIN_HOT_FRACTION:
            avg_hot = float(hot.mean())
        else:
            avg_hot = None

        emit_payload = {
            "t": int(t * 1000),
            "ambient": round(ambient, 2),
            "temps": temps,
            "stats": {
                "avg_all_c": round(float(arr.mean()), 2),
                "avg_hot_c": round(avg_hot, 2) if avg_hot is not None else None,
                "max_c": round(float(arr.max()), 2),
                "min_c": round(float(arr.min()), 2),
            },
        }
        try:
            from app import socketio
            socketio.emit("thermal_frame", emit_payload, namespace="/ws/stream")
        except Exception as e:
            log.debug("broadcast thermal failed: %s", e)
