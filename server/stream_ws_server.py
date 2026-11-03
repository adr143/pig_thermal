from __future__ import annotations

import json
import logging
import math
import queue
from typing import Any

from flask import request
from flask_sock import Sock
from simple_websocket import ConnectionClosed

from live_state import state
from websocket_hub import send_event, subscribe, unsubscribe

log = logging.getLogger("stream")


def register_stream_route(sock: Sock) -> None:
    @sock.route("/ws/stream")
    def stream_socket(ws: Any) -> None:
        client_queue = subscribe()
        clients = state.inc_browser_client()
        log.info("browser connected from %s (clients=%d)",
                 request.remote_addr, clients)
        try:
            ws.send(send_event("hello", {
                "msg": "connected",
                "thermal_clients": state.thermal_clients,
                "browser_clients": clients,
                "thermal_age_s": _thermal_age_s(),
            }))
            while True:
                try:
                    message = ws.receive(timeout=0.1)
                except TimeoutError:
                    message = None
                except ConnectionClosed:
                    break

                if message is not None:
                    _handle_command(ws, message)

                try:
                    ws.send(client_queue.get_nowait())
                except queue.Empty:
                    pass
        except ConnectionClosed:
            pass
        except Exception:
            log.exception("stream: WebSocket connection failed")
        finally:
            unsubscribe(client_queue)
            clients = state.dec_browser_client()
            log.info("browser disconnected (clients=%d)", clients)


def _handle_command(ws: Any, message: Any) -> None:
    if isinstance(message, bytes):
        try:
            message = message.decode("utf-8")
        except UnicodeDecodeError:
            log.warning("stream: received non-UTF-8 binary command")
            return
    if not isinstance(message, str):
        log.warning("stream: received unsupported command type")
        return

    try:
        command = json.loads(message)
    except json.JSONDecodeError:
        log.warning("stream: received invalid JSON command")
        return
    if not isinstance(command, dict):
        log.warning("stream: expected a JSON command object")
        return

    command_type = command.get("type")
    if command_type == "take_snapshot":
        from snapshot import trigger_manual_snapshot

        log.info("snapshot triggered via raw WebSocket")
        trigger_manual_snapshot()
    elif command_type == "request_stats":
        _, _, stats = state.get_webcam()
        ws.send(send_event("stats_update", {
            "pig_count": stats.pig_count,
            "classifications": stats.classifications,
            "avg_thermal_c": stats.avg_thermal_c,
            "thermal_age_s": _thermal_age_s(),
            "thermal_clients": state.thermal_clients,
        }))


def _thermal_age_s() -> float | None:
    age = state.thermal_age_s()
    return round(age, 1) if math.isfinite(age) else None
