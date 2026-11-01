"""
Shared state for live frames + stats, with simple threading.Lock guards.

Modules:
  - thermal_capture : last received MLX90640 frame (768 floats + ambient)
  - webcam_state    : last webcam JPEG (annotated) + detections + stats
  - snapshot hub    : channels for streaming to browser clients

This is a thin in-memory cache. Raw WebSocket broadcasts happen in
stream_ws_server.py (webcam thread) and thermal_ws_server.py (ESP32 receive).
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import numpy as np

from config import config


@dataclass
class ThermalFrame:
    temps: List[float]        # 768 floats, row-major (32 wide x 24 tall)
    ambient: float
    received_at: float       # epoch seconds
    source: str = "esp32"


@dataclass
class PigLive:
    bbox: List[int]                  # x1, y1, x2, y2
    detection_conf: float
    classification: Optional[str] = None
    class_confidence: Optional[float] = None


@dataclass
class WebcamStats:
    pig_count: int = 0
    classifications: Dict[str, int] = field(default_factory=dict)
    avg_thermal_c: Optional[float] = None
    updated_at: float = 0.0


class LiveState:
    """Thread-safe container for live frames + stats."""

    def __init__(self) -> None:
        self._lock = threading.RLock()

        self.thermal: Optional[ThermalFrame] = None
        self.thermal_last_seen: float = 0.0
        self.thermal_source: str = "none"

        # Latest annotated webcam frame (JPEG bytes) for HTTP fallback / poll
        self.webcam_jpeg: Optional[bytes] = None
        self.webcam_detections: List[PigLive] = []
        self.webcam_stats = WebcamStats()

        # Current browser and ESP32 WebSocket connection counts.
        self.browser_clients: int = 0
        self.thermal_clients: int = 0  # ESP32 nodes connected

    # -- thermal --
    def set_thermal(self, frame: ThermalFrame) -> None:
        with self._lock:
            self.thermal = frame
            self.thermal_last_seen = frame.received_at
            self.thermal_source = frame.source

    def get_thermal(self) -> Optional[ThermalFrame]:
        with self._lock:
            return self.thermal

    def thermal_age_s(self) -> float:
        with self._lock:
            if self.thermal_last_seen == 0:
                return float("inf")
            return time.time() - self.thermal_last_seen

    # -- webcam --
    def set_webcam(self, jpeg: bytes, dets: List[PigLive], stats: WebcamStats) -> None:
        with self._lock:
            self.webcam_jpeg = jpeg
            self.webcam_detections = dets
            self.webcam_stats = stats

    def get_webcam(self) -> tuple:
        with self._lock:
            return self.webcam_jpeg, list(self.webcam_detections), self.webcam_stats

    # -- counters --
    def inc_browser_client(self) -> int:
        with self._lock:
            self.browser_clients += 1
            return self.browser_clients

    def dec_browser_client(self) -> int:
        with self._lock:
            self.browser_clients = max(0, self.browser_clients - 1)
            return self.browser_clients

    def inc_thermal_client(self) -> int:
        with self._lock:
            self.thermal_clients += 1
            return self.thermal_clients

    def dec_thermal_client(self) -> int:
        with self._lock:
            self.thermal_clients = max(0, self.thermal_clients - 1)
            return self.thermal_clients


# Singleton
state = LiveState()
