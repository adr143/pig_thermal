"""
Webcam capture thread.

Continuously reads frames from the webcam, runs inference every Nth frame,
annotates bounding boxes + class labels, JPEG-encodes, and emits
'webcam_frame' events to all browser clients via flask-socketio.

Also computes pig count + classification stats and updates LiveState.
"""
from __future__ import annotations

import base64
import logging
import time
from collections import Counter
from typing import List, Optional

import cv2
import numpy as np

from config import config
from inference import detect_and_classify, models_loaded, load_models
from live_state import PigLive, WebcamStats, state

log = logging.getLogger("webcam")

# Track inference models across reloads
_MODELS_TRIED = False


def _ensure_models() -> None:
    global _MODELS_TRIED
    if _MODELS_TRIED:
        return
    _MODELS_TRIED = True
    try:
        load_models()
    except Exception as e:
        log.error("model load failed: %s", e)


def _annotate(frame: np.ndarray, dets) -> np.ndarray:
    """Draw bboxes + labels on a copy of the frame."""
    out = frame.copy()
    # Color palette for different classes
    palette = [
        (0, 255, 0), (255, 165, 0), (255, 0, 255),
        (255, 255, 0), (0, 255, 255), (160, 32, 240),
    ]
    for i, d in enumerate(dets):
        x1, y1, x2, y2 = d.bbox
        color = palette[i % len(palette)]
        cv2.rectangle(out, (x1, y1), (x2, y2), color, 2)
        label = d.classification or "pig"
        conf = d.class_confidence if d.class_confidence is not None else d.confidence
        txt = f"{label} {conf:.2f}"
        (tw, th), _ = cv2.getTextSize(txt, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.rectangle(out, (x1, y1 - th - 6), (x1 + tw + 6, y1), color, -1)
        cv2.putText(out, txt, (x1 + 3, y1 - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 1, cv2.LINE_AA)
    return out


def _build_stats(dets) -> WebcamStats:
    classes = Counter()
    for d in dets:
        label = d.classification or "unknown"
        classes[label] += 1
    stats = WebcamStats(
        pig_count=len(dets),
        classifications=dict(classes),
        updated_at=time.time(),
    )
    return stats


def _compute_avg_thermal() -> Optional[float]:
    """Compute avg temp of 'pig body' pixels (above threshold).

    Falls back to avg of all pixels if not enough hot pixels.
    Returns None if no thermal frame available.
    """
    tf = state.get_thermal()
    if tf is None:
        return None
    arr = np.asarray(tf.temps, dtype=np.float32)
    hot = arr[arr >= config.THERMAL_PIG_THRESHOLD_C]
    if hot.size / arr.size >= config.THERMAL_MIN_HOT_FRACTION:
        return float(hot.mean())
    return float(arr.mean())


def _jpg_encode(frame: np.ndarray) -> bytes:
    ok, buf = cv2.imencode(
        ".jpg", frame,
        [int(cv2.IMWRITE_JPEG_QUALITY), config.WEBCAM_JPEG_QUALITY],
    )
    if not ok:
        return b""
    return buf.tobytes()


def webcam_loop(socketio, stop_event) -> None:
    """Main webcam capture + inference + emit loop. Runs in a thread."""
    _ensure_models()

    log.info("webcam: opening VideoCapture(%d)", config.WEBCAM_INDEX)
    cap = cv2.VideoCapture(config.WEBCAM_INDEX)
    if not cap.isOpened():
        log.error("webcam: cannot open VideoCapture(%d). "
                  "Check device index / permissions.", config.WEBCAM_INDEX)
        return

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.WEBCAM_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.WEBCAM_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, config.WEBCAM_FPS)
    actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    log.info("webcam: opened at %dx%d", actual_w, actual_h)

    frame_idx = 0
    last_dets: List = []
    last_stats = WebcamStats()
    last_emit_ms = 0

    try:
        while not stop_event.is_set():
            ok, frame = cap.read()
            if not ok:
                log.warning("webcam: read failed, retrying in 200ms")
                time.sleep(0.2)
                continue

            frame_idx += 1

            # Run inference on every Nth frame to bound CPU usage
            if frame_idx % config.INFERENCE_EVERY_N_FRAMES == 0:
                try:
                    last_dets = detect_and_classify(frame)
                    last_stats = _build_stats(last_dets)
                    last_stats.avg_thermal_c = _compute_avg_thermal()
                except Exception as e:
                    log.exception("inference failed: %s", e)

            # Always annotate using last detections so the stream stays smooth
            annotated = _annotate(frame, last_dets) if last_dets else frame
            jpg = _jpg_encode(annotated)

            state.set_webcam(
                jpeg=jpg,
                dets=[
                    PigLive(
                        bbox=list(d.bbox),
                        detection_conf=d.confidence,
                        classification=d.classification,
                        class_confidence=d.class_confidence,
                    ) for d in last_dets
                ],
                stats=last_stats,
            )

            # Throttle broadcasts (no need to flood clients if FPS > broadcast cap)
            now_ms = int(time.time() * 1000)
            if now_ms - last_emit_ms < config.WEBCAM_BROADCAST_MIN_MS:
                continue
            last_emit_ms = now_ms

            try:
                socketio.emit(
                    "webcam_frame",
                    {
                        "img": "data:image/jpeg;base64," + base64.b64encode(jpg).decode("ascii"),
                        "t": int(time.time() * 1000),
                        "stats": {
                            "pig_count": last_stats.pig_count,
                            "classifications": last_stats.classifications,
                            "avg_thermal_c": last_stats.avg_thermal_c,
                            "models_loaded": models_loaded(),
                        },
                        "detections": [
                            {
                                "bbox": list(d.bbox),
                                "conf": d.confidence,
                                "cls": d.classification,
                                "cls_conf": d.class_confidence,
                            }
                            for d in last_dets
                        ],
                    },
                    namespace="/ws/stream",
                )
            except Exception as e:
                log.debug("emit webcam_frame failed: %s", e)
    finally:
        cap.release()
        log.info("webcam: loop stopped, capture released")
