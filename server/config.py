from __future__ import annotations

import os
from dataclasses import dataclass

from dotenv import load_dotenv

load_dotenv()


def _bool_env(name: str, default: bool) -> bool:
    val = os.getenv(name, str(default)).strip().lower()
    return val in ("1", "true", "yes", "on")


def _int_env(name: str, default: int) -> int:
    return int(os.getenv(name, default))


def _float_env(name: str, default: float) -> float:
    return float(os.getenv(name, default))


@dataclass
class Config:

    HOST: str = os.getenv("HOST", "0.0.0.0")
    PORT: int = _int_env("PORT", 5000)
    DEBUG: bool = _bool_env("DEBUG", False)

    DB_PATH: str = os.getenv("DB_PATH", "pigs.db")


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


    ALLOW_MISSING_MODELS: bool = _bool_env("ALLOW_MISSING_MODELS", True)


    WEBCAM_INDEX: int = _int_env("WEBCAM_INDEX", 0)
    WEBCAM_WIDTH: int = _int_env("WEBCAM_WIDTH", 1280)
    WEBCAM_HEIGHT: int = _int_env("WEBCAM_HEIGHT", 720)
    WEBCAM_FPS: int = _int_env("WEBCAM_FPS", 15)

    WEBCAM_JPEG_QUALITY: int = _int_env("WEBCAM_JPEG_QUALITY", 70)

    INFERENCE_EVERY_N_FRAMES: int = _int_env("INFERENCE_EVERY_N_FRAMES", 3)


    THERMAL_WIDTH: int = 32
    THERMAL_HEIGHT: int = 24


    THERMAL_PIG_THRESHOLD_C: float = _float_env("THERMAL_PIG_THRESHOLD_C", 30.0)

    THERMAL_MIN_HOT_FRACTION: float = _float_env("THERMAL_MIN_HOT_FRACTION", 0.05)


    SNAPSHOTS_DIR: str = os.getenv("SNAPSHOTS_DIR", "snapshots")

    DEFAULT_SCHEDULE_ENABLED: bool = _bool_env("DEFAULT_SCHEDULE_ENABLED", False)


    DEFAULT_SCHEDULE_MODE: str = os.getenv("DEFAULT_SCHEDULE_MODE", "interval")
    DEFAULT_SCHEDULE_INTERVAL_MIN: int = _int_env("DEFAULT_SCHEDULE_INTERVAL_MIN", 15)
    DEFAULT_SCHEDULE_CRON_EXPR: str = os.getenv(
        "DEFAULT_SCHEDULE_CRON_EXPR", "0 8 * * *"
    )


    THERMAL_BROADCAST_MIN_MS: int = _int_env("THERMAL_BROADCAST_MIN_MS", 250)

    WEBCAM_BROADCAST_MIN_MS: int = _int_env("WEBCAM_BROADCAST_MIN_MS", 100)


config = Config()
