"""
YOLO inference wrapper.

Loads two Ultralytics models:
  - DETECT_MODEL_PATH : object detection (pigs)
  - CLASSIFY_MODEL_PATH: classification (pig class)

Falls back gracefully if model files are missing (config.ALLOW_MISSING_MODELS).
"""
from __future__ import annotations

import logging
import os
import threading
from dataclasses import dataclass
from typing import List, Optional, Tuple

import cv2
import numpy as np

from config import config

log = logging.getLogger("inference")

# Lazy-imported so the app can boot even without ultralytics installed
_DETECT_MODEL = None
_CLASSIFY_MODEL = None
_MODELS_OK = False
_LOAD_LOCK = threading.Lock()


@dataclass
class PigDetection:
    bbox: Tuple[int, int, int, int]      # x1, y1, x2, y2 in source-pixel coords
    confidence: float
    classification: Optional[str] = None
    class_confidence: Optional[float] = None


def models_loaded() -> bool:
    return _MODELS_OK


def load_models() -> None:
    """Lazy-load models. Safe to call multiple times."""
    global _DETECT_MODEL, _CLASSIFY_MODEL, _MODELS_OK
    with _LOAD_LOCK:
        if _MODELS_OK:
            return
        try:
            from ultralytics import YOLO  # noqa
        except ImportError as e:
            log.warning("ultralytics not installed (%s); inference disabled", e)
            return

        det_path = config.DETECT_MODEL_PATH
        cls_path = config.CLASSIFY_MODEL_PATH

        if not os.path.isfile(det_path):
            msg = f"detect model not found at {det_path}"
            if config.ALLOW_MISSING_MODELS:
                log.warning("%s — running without detection", msg)
            else:
                raise FileNotFoundError(msg)
        else:
            log.info("loading detect model: %s", det_path)
            _DETECT_MODEL = YOLO(det_path)

        if not os.path.isfile(cls_path):
            msg = f"classify model not found at {cls_path}"
            if config.ALLOW_MISSING_MODELS:
                log.warning("%s — running without classification", msg)
            else:
                raise FileNotFoundError(msg)
        else:
            log.info("loading classify model: %s", cls_path)
            _CLASSIFY_MODEL = YOLO(cls_path)

        _MODELS_OK = True
        log.info("models ready (detect=%s, classify=%s)",
                 _DETECT_MODEL is not None, _CLASSIFY_MODEL is not None)


def detect_pigs(frame_bgr: np.ndarray) -> List[PigDetection]:
    """
    Run pig detection on a BGR frame. Returns list of detections.
    Empty list if detector missing or no confident hits.
    """
    if _DETECT_MODEL is None:
        return []
    try:
        results = _DETECT_MODEL.predict(
            source=frame_bgr,
            conf=config.DETECT_CONF,
            iou=config.DETECT_IOU,
            imgsz=config.DETECT_IMG_SIZE,
            verbose=False,
            device="cpu",   # change to 0 for CUDA, "mps" for Apple Silicon
        )
    except Exception as e:
        log.exception("detect failed: %s", e)
        return []

    out: List[PigDetection] = []
    for r in results:
        boxes = getattr(r, "boxes", None)
        if boxes is None:
            continue
        xyxy = boxes.xyxy.cpu().numpy() if hasattr(boxes.xyxy, "cpu") else np.asarray(boxes.xyxy)
        confs = boxes.conf.cpu().numpy() if hasattr(boxes.conf, "cpu") else np.asarray(boxes.conf)
        for (x1, y1, x2, y2), conf in zip(xyxy, confs):
            x1i, y1i, x2i, y2i = map(int, (x1, y1, x2, y2))
            # Clamp to image bounds
            x1i = max(0, min(frame_bgr.shape[1] - 1, x1i))
            y1i = max(0, min(frame_bgr.shape[0] - 1, y1i))
            x2i = max(0, min(frame_bgr.shape[1], x2i))
            y2i = max(0, min(frame_bgr.shape[0], y2i))
            if x2i - x1i < 4 or y2i - y1i < 4:
                continue
            out.append(PigDetection(bbox=(x1i, y1i, x2i, y2i), confidence=float(conf)))
    return out


def classify_crop(crop_bgr: np.ndarray) -> Tuple[Optional[str], Optional[float]]:
    """
    Run classification on a BGR crop. Returns (label, confidence) or
    (None, None) if classifier missing.
    """
    if _CLASSIFY_MODEL is None or crop_bgr.size == 0:
        return None, None
    try:
        results = _CLASSIFY_MODEL.predict(
            source=crop_bgr,
            imgsz=config.CLASSIFY_IMG_SIZE,
            verbose=False,
            device="cpu",
        )
    except Exception as e:
        log.exception("classify failed: %s", e)
        return None, None
    for r in results:
        names = getattr(r, "names", None) or {}
        probs = getattr(r, "probs", None)
        if probs is None:
            continue
        top1 = int(getattr(probs, "top1", 0))
        conf = float(getattr(probs, "top1conf", 0.0))
        label = names.get(top1, f"class_{top1}")
        return label, conf
    return None, None


def detect_and_classify(frame_bgr: np.ndarray) -> List[PigDetection]:
    """
    One-shot detect + classify. Each pig crop is classified.
    Mutates PigDetection.classification / class_confidence fields.
    """
    dets = detect_pigs(frame_bgr)
    if not dets or _CLASSIFY_MODEL is None:
        return dets
    for d in dets:
        x1, y1, x2, y2 = d.bbox
        crop = frame_bgr[y1:y2, x1:x2]
        label, conf = classify_crop(crop)
        d.classification = label
        d.class_confidence = conf
    return dets
