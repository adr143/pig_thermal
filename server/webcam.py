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
from websocket_hub import broadcast

log = logging.getLogger("webcam")


_MODELS_TRIED = False
_READ_FAILURES_BEFORE_RECONNECT = 5
_RECONNECT_DELAY_S = 2.0
_READ_RETRY_DELAY_S = 0.2


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
    out = frame.copy()

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


def webcam_loop(stop_event) -> None:
    _ensure_models()

    frame_idx = 0
    last_dets: List = []
    last_stats = WebcamStats()
    last_emit_ms = 0

    while not stop_event.is_set():
        log.info("webcam: opening VideoCapture(%d)", config.WEBCAM_INDEX)
        cap = cv2.VideoCapture(config.WEBCAM_INDEX)
        if not cap.isOpened():
            log.error(
                "webcam: cannot open VideoCapture(%d); retrying in %.1f seconds",
                config.WEBCAM_INDEX,
                _RECONNECT_DELAY_S,
            )
            cap.release()
            stop_event.wait(_RECONNECT_DELAY_S)
            continue

        cap.set(cv2.CAP_PROP_FRAME_WIDTH, config.WEBCAM_WIDTH)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, config.WEBCAM_HEIGHT)
        cap.set(cv2.CAP_PROP_FPS, config.WEBCAM_FPS)
        actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        log.info("webcam: opened at %dx%d", actual_w, actual_h)

        consecutive_read_failures = 0
        try:
            while not stop_event.is_set():
                ok, frame = cap.read()
                if not ok:
                    consecutive_read_failures += 1
                    if consecutive_read_failures >= _READ_FAILURES_BEFORE_RECONNECT:
                        log.warning(
                            "webcam: %d consecutive frame reads failed; "
                            "restarting VideoCapture",
                            consecutive_read_failures,
                        )
                        break
                    log.warning(
                        "webcam: frame read failed (%d/%d), retrying in %.1f seconds",
                        consecutive_read_failures,
                        _READ_FAILURES_BEFORE_RECONNECT,
                        _READ_RETRY_DELAY_S,
                    )
                    stop_event.wait(_READ_RETRY_DELAY_S)
                    continue

                consecutive_read_failures = 0

                frame_idx += 1


                if frame_idx % config.INFERENCE_EVERY_N_FRAMES == 0:
                    try:
                        last_dets = detect_and_classify(frame)
                        last_stats = _build_stats(last_dets)
                        last_stats.avg_thermal_c = _compute_avg_thermal()
                    except Exception as e:
                        log.exception("inference failed: %s", e)


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


                now_ms = int(time.time() * 1000)
                if now_ms - last_emit_ms < config.WEBCAM_BROADCAST_MIN_MS:
                    continue
                last_emit_ms = now_ms

                try:
                    broadcast(
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
                    )
                except Exception as e:
                    log.debug("broadcast webcam_frame failed: %s", e)
        finally:
            cap.release()
            log.info("webcam: capture released")

        if not stop_event.is_set():
            log.info("webcam: reopening capture in %.1f seconds", _RECONNECT_DELAY_S)
            stop_event.wait(_RECONNECT_DELAY_S)
