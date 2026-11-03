from __future__ import annotations

import json
import logging
import queue
import threading
from typing import Any

log = logging.getLogger("websocket_hub")

_clients_lock = threading.Lock()
_FRAME_EVENTS = ("thermal_frame", "webcam_frame")


class StreamMailbox:
    def __init__(self) -> None:
        self._events: queue.Queue[str] = queue.Queue()
        self._frames = {
            event: queue.Queue[str](maxsize=1)
            for event in _FRAME_EVENTS
        }

    def publish(self, event: str, message: str) -> None:
        if event not in self._frames:
            self._events.put_nowait(message)
            return

        frame_queue = self._frames[event]
        try:
            frame_queue.put_nowait(message)
        except queue.Full:
            try:
                frame_queue.get_nowait()
            except queue.Empty:
                pass
            frame_queue.put_nowait(message)

    def get_nowait(self) -> str:
        try:
            return self._events.get_nowait()
        except queue.Empty:
            for event in _FRAME_EVENTS:
                try:
                    return self._frames[event].get_nowait()
                except queue.Empty:
                    continue
        raise queue.Empty


_clients: set[StreamMailbox] = set()


def subscribe() -> StreamMailbox:
    client_queue = StreamMailbox()
    with _clients_lock:
        _clients.add(client_queue)
    return client_queue


def unsubscribe(client_queue: StreamMailbox) -> None:
    with _clients_lock:
        _clients.discard(client_queue)


def client_count() -> int:
    with _clients_lock:
        return len(_clients)


def send_event(event: str, payload: dict[str, Any]) -> str:
    return json.dumps({"type": event, **payload}, separators=(",", ":"), allow_nan=False)


def broadcast(event: str, payload: dict[str, Any]) -> None:
    message = send_event(event, payload)
    with _clients_lock:
        clients = tuple(_clients)
        for client_queue in clients:
            client_queue.publish(event, message)
