"""
Browser-facing WebSocket namespace '/ws/stream'.

Clients connect here to receive:
  - 'webcam_frame' : { img: 'data:image/jpeg;base64,...', t, stats, detections }
  - 'thermal_frame': { t, ambient, temps[768], stats }
  - 'snapshot_taken' : { snapshot_id, captured_at, source, pig_count, ... }
  - 'hello' (response to client hello)

Clients can emit:
  - 'take_snapshot'  -> triggers an immediate snapshot
  - 'request_stats'  -> replies with current stats
"""
from __future__ import annotations

import logging

from flask import request
from flask_socketio import Namespace, emit

from live_state import state

log = logging.getLogger("stream")


class StreamNamespace(Namespace):
    def on_connect(self, *args, **kwargs):
        state.inc_browser_client()
        log.info("browser connected from %s (clients=%d)",
                 request.remote_addr, state.browser_clients)
        emit("hello", {
            "msg": "connected",
            "thermal_clients": state.thermal_clients,
            "browser_clients": state.browser_clients,
            "thermal_age_s": round(state.thermal_age_s(), 1),
        })

    def on_disconnect(self, *args, **kwargs):
        state.dec_browser_client()
        log.info("browser disconnected (clients=%d)", state.browser_clients)

    def on_take_snapshot(self, data):
        # Triggered by the "Snapshot" button on the UI
        from snapshot import trigger_manual_snapshot
        log.info("snapshot triggered via WS by client")
        result = trigger_manual_snapshot()
        emit("snapshot_taken", result, broadcast=True, include_self=True)

    def on_request_stats(self, _):
        jpeg, dets, stats = state.get_webcam()
        emit("stats_update", {
            "pig_count": stats.pig_count,
            "classifications": stats.classifications,
            "avg_thermal_c": stats.avg_thermal_c,
            "thermal_age_s": round(state.thermal_age_s(), 1),
            "thermal_clients": state.thermal_clients,
        })
