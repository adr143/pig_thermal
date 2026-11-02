"""
Central configuration for the Pig Monitoring System backend.

All values can be overridden via environment variables (see .env file).
"""
from __future__ import annotations

import os
from dataclasses import dataclass

from dotenv import load_dotenv

load_dotenv()  # reads .env if present


def _bool_env(name: str, default: bool) -> bool:
    val = os.getenv(name, str(default)).strip().lower()
    return val in ("1", "true", "yes", "on")


def _int_env(name: str, default: int) -> int:
    return int(os.getenv(name, default))


def _float_env(name: str, default: float) -> float:
    return float(os.getenv(name, default))


@dataclass
class Config:
    # ----- Server -----
    HOST: str = os.getenv("HOST", "0.0.0.0")
    PORT: int = _int_env("PORT", 5000)
    DEBUG: bool = _bool_env("DEBUG", False)
    # ----- Database -----
    DB_PATH: str = os.getenv("DB_PATH", "pigs.db")

    # ----- Models -----
    # Ultralytics framework loads any YOLOv8/9/10/11/12 .pt file.
    # Drop your trained .pt files into ./models and reference them here.
    DETECT_MODEL_PATH: str = os.getenv(
        "DETECT_MODEL_PATH", "models/pig_detect.pt"
    )
    CLASSIFY_MODEL_PATH: str = os.getenv(
        "CLASSIFY_MODEL_PATH", "models/pig_classify.pt"
    )
    DETECT_CONF: float = _float_env("DETECT_CONF", 0.35)
    DETECT_IOU: float = _float_env("DETECT_IOU", 0.5)
    DETECT_IMG_SIZE: int = _int_env("DETECT_IMG_SIZE", 640)
    CLASSIFY_IMG_SIZE: int = _int_env("CLASSIFY_IMG_SIZE", 224)

    # Allow app to boot even if model files are missing (shows raw webcam
    # stream with no overlays, no stats). Set to False in production.
    ALLOW_MISSING_MODELS: bool = _bool_env("ALLOW_MISSING_MODELS", True)

    # ----- Webcam -----
    WEBCAM_INDEX: int = _int_env("WEBCAM_INDEX", 0)
    WEBCAM_WIDTH: int = _int_env("WEBCAM_WIDTH", 1280)
    WEBCAM_HEIGHT: int = _int_env("WEBCAM_HEIGHT", 720)
    WEBCAM_FPS: int = _int_env("WEBCAM_FPS", 15)
    # JPEG quality for stream frames (1-100)
    WEBCAM_JPEG_QUALITY: int = _int_env("WEBCAM_JPEG_QUALITY", 70)
    # Skip frames between inference passes (1 = every frame, 5 = every 5th)
    INFERENCE_EVERY_N_FRAMES: int = _int_env("INFERENCE_EVERY_N_FRAMES", 3)

    # ----- Thermal -----
    # Thermal frame is 32x24 from MLX90640
    THERMAL_WIDTH: int = 32
    THERMAL_HEIGHT: int = 24
    THERMAL_SERIAL_PORT: str = os.getenv("THERMAL_SERIAL_PORT", "COM3").strip()
    THERMAL_SERIAL_BAUD: int = _int_env("THERMAL_SERIAL_BAUD", 115200)
    # Pixels above this threshold are considered "pig body" for avg-temp proxy
    # Adjust to your environment (typical pig surface temp ~ 32-38 C)
    THERMAL_PIG_THRESHOLD_C: float = _float_env("THERMAL_PIG_THRESHOLD_C", 30.0)
    # Min fraction of frame above threshold to consider it "pigs present"
    THERMAL_MIN_HOT_FRACTION: float = _float_env("THERMAL_MIN_HOT_FRACTION", 0.05)

    # ----- Snapshots -----
    SNAPSHOTS_DIR: str = os.getenv("SNAPSHOTS_DIR", "snapshots")
    # Default scheduler — overridable at runtime via API
    DEFAULT_SCHEDULE_ENABLED: bool = _bool_env("DEFAULT_SCHEDULE_ENABLED", False)
    # Interval mode: "interval" or "cron"
    #   interval: every N minutes (15 or 60 typical)
    #   cron:     specific time-of-day (HH:MM)
    DEFAULT_SCHEDULE_MODE: str = os.getenv("DEFAULT_SCHEDULE_MODE", "interval")
    DEFAULT_SCHEDULE_INTERVAL_MIN: int = _int_env("DEFAULT_SCHEDULE_INTERVAL_MIN", 15)
    DEFAULT_SCHEDULE_CRON_EXPR: str = os.getenv(
        "DEFAULT_SCHEDULE_CRON_EXPR", "0 8 * * *"   # 08:00 daily
    )

    # ----- Stream throttle -----
    # Min ms between thermal frame broadcasts to browsers
    THERMAL_BROADCAST_MIN_MS: int = _int_env("THERMAL_BROADCAST_MIN_MS", 250)
    # Min ms between webcam frame broadcasts to browsers
    WEBCAM_BROADCAST_MIN_MS: int = _int_env("WEBCAM_BROADCAST_MIN_MS", 100)


config = Config()
