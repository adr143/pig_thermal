"""
Snapshot capture + scheduler.

- trigger_manual_snapshot(): captures webcam + thermal + inference result,
  saves JPEG to disk, inserts DB rows, returns dict for broadcast.
- SnapshotScheduler: wraps APScheduler, supports two modes:
    interval:  every N minutes (e.g. 15 or 60)
    cron:      standard cron expression (e.g. "0 8 * * *" = daily 08:00)
  Persists state in DB so restarts keep config.
"""
from __future__ import annotations

import logging
import os
import time
from datetime import datetime, timezone
from typing import Any, Dict, List, Optional

import cv2
import numpy as np
from apscheduler.schedulers.background import BackgroundScheduler
from apscheduler.triggers.cron import CronTrigger
from apscheduler.triggers.interval import IntervalTrigger

import db
from config import config
from inference import detect_and_classify, load_models
from live_state import state

log = logging.getLogger("snapshot")

_scheduler: Optional[BackgroundScheduler] = None
_job = None  # current scheduled job

# Try to import socketio lazily to avoid circular import
def _socketio():
    try:
        from app import socketio
        return socketio
    except Exception:
        return None


def _snapshots_dir() -> str:
    os.makedirs(config.SNAPSHOTS_DIR, exist_ok=True)
    return config.SNAPSHOTS_DIR


def _now_iso_utc() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _ts_for_filename() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")


# ---------------------------------------------------------------------------
# Capture + persist
# ---------------------------------------------------------------------------
def capture_current_frame() -> Optional[np.ndarray]:
    """Grab a fresh frame from the webcam (not the live cache) for full-res."""
    cap = cv2.VideoCapture(config.WEBCAM_INDEX)
    if not cap.isOpened():
        log.error("snapshot: cannot open webcam for capture")
        return None
    # Warm up: discard a few frames so AE/AWB settles
    frame = None
    for _ in range(5):
        ok, frame = cap.read()
        if not ok:
            cap.release()
            return None
    cap.release()
    return frame


def _draw_overlay(frame: np.ndarray, dets: List) -> np.ndarray:
    out = frame.copy()
    palette = [(0, 255, 0), (255, 165, 0), (255, 0, 255),
               (255, 255, 0), (0, 255, 255), (160, 32, 240)]
    for i, d in enumerate(dets):
        x1, y1, x2, y2 = d.bbox
        color = palette[i % len(palette)]
        cv2.rectangle(out, (x1, y1), (x2, y2), color, 3)
        label = d.classification or "pig"
        conf = d.class_confidence if d.class_confidence is not None else d.confidence
        txt = f"{label} {conf:.2f}"
        (tw, th), _ = cv2.getTextSize(txt, cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)
        cv2.rectangle(out, (x1, y1 - th - 8), (x1 + tw + 8, y1), color, -1)
        cv2.putText(out, txt, (x1 + 4, y1 - 6),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 0), 2, cv2.LINE_AA)
    # Timestamp overlay
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cv2.putText(out, ts, (10, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2, cv2.LINE_AA)
    return out


def _save_jpeg(frame: np.ndarray, name_base: str) -> str:
    fname = f"{name_base}.jpg"
    path = os.path.join(_snapshots_dir(), fname)
    cv2.imwrite(path, frame, [int(cv2.IMWRITE_JPEG_QUALITY), 92])
    return path


def trigger_manual_snapshot() -> Dict[str, Any]:
    """Take a snapshot NOW (button press). Returns dict for broadcast + DB."""
    return _do_capture(source="button")


def trigger_scheduled_snapshot() -> Dict[str, Any]:
    """Take a snapshot (cron/interval trigger). Returns dict."""
    return _do_capture(source="schedule")


def _do_capture(source: str) -> Dict[str, Any]:
    try:
        load_models()
    except Exception as e:
        log.error("snapshot: model load failed: %s", e)

    frame = capture_current_frame()
    if frame is None:
        log.error("snapshot: no webcam frame available")
        return {"ok": False, "error": "no_webcam_frame"}

    # Run detection + classification on the captured frame
    try:
        dets = detect_and_classify(frame)
    except Exception as e:
        log.exception("snapshot: inference failed: %s", e)
        dets = []

    overlay = _draw_overlay(frame, dets)
    name_base = f"snap_{_ts_for_filename()}_{source}"
    rel_path = _save_jpeg(overlay, name_base)

    # Avg thermal (cached; live thermal frame is fine for snapshot context)
    avg_thermal = None
    tf = state.get_thermal()
    if tf is not None:
        arr = np.asarray(tf.temps, dtype=np.float32)
        hot = arr[arr >= config.THERMAL_PIG_THRESHOLD_C]
        if hot.size / arr.size >= config.THERMAL_MIN_HOT_FRACTION:
            avg_thermal = float(hot.mean())
        else:
            avg_thermal = float(arr.mean())

    pigs_payload = [
        {
            "bbox_x1": int(d.bbox[0]),
            "bbox_y1": int(d.bbox[1]),
            "bbox_x2": int(d.bbox[2]),
            "bbox_y2": int(d.bbox[3]),
            "detect_conf": float(d.confidence),
            "classification": d.classification,
            "class_conf": float(d.class_confidence) if d.class_confidence is not None else None,
        }
        for d in dets
    ]

    snap_id = db.insert_snapshot(
        captured_at=_now_iso_utc(),
        source=source,
        pig_count=len(dets),
        avg_thermal_c=avg_thermal,
        snapshot_path=rel_path,
        pigs=pigs_payload,
    )

    log.info("snapshot #%d saved (%s, %d pigs, avg=%.2f C, file=%s)",
             snap_id, source, len(dets),
             avg_thermal if avg_thermal is not None else -1, rel_path)

    result = {
        "ok": True,
        "snapshot_id": snap_id,
        "captured_at": _now_iso_utc(),
        "source": source,
        "pig_count": len(dets),
        "avg_thermal_c": round(avg_thermal, 2) if avg_thermal is not None else None,
        "snapshot_path": rel_path,
        "pigs": [
            {
                "id": None,  # frontend can fetch detail to get IDs
                "classification": p["classification"],
                "class_conf": p["class_conf"],
                "detect_conf": p["detect_conf"],
                "temperature_c": None,  # blank, awaits manual input
                "bbox": [p["bbox_x1"], p["bbox_y1"], p["bbox_x2"], p["bbox_y2"]],
            }
            for p in pigs_payload
        ],
    }

    # Broadcast to all browser clients
    sock = _socketio()
    if sock is not None:
        try:
            sock.emit("snapshot_taken", result, namespace="/ws/stream")
        except Exception as e:
            log.debug("broadcast snapshot_taken failed: %s", e)

    return result


# ---------------------------------------------------------------------------
# Scheduler
# ---------------------------------------------------------------------------
def _schedule_next_run_text() -> Optional[str]:
    if _job is None or _scheduler is None:
        return None
    try:
        n = _job.next_run_time
        if n is None:
            return None
        return n.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    except Exception:
        return None


def _apply_state(state_dict: Dict[str, Any]) -> None:
    """Re-create the APScheduler job from DB state."""
    global _job
    if _scheduler is None:
        return
    # Remove old job
    if _job is not None:
        try:
            _scheduler.remove_job(_job.id)
        except Exception:
            pass
        _job = None

    if not bool(state_dict.get("enabled", 0)):
        return

    mode = state_dict.get("mode", "interval")
    if mode == "interval":
        mins = int(state_dict.get("interval_min", 15) or 15)
        mins = max(1, mins)
        trigger = IntervalTrigger(minutes=mins)
    else:
        expr = state_dict.get("cron_expr", "0 8 * * *")
        try:
            trigger = CronTrigger.from_crontab(expr)
        except Exception as e:
            log.error("snapshot: bad cron expr %r: %s", expr, e)
            return

    _job = _scheduler.add_job(
        trigger_scheduled_snapshot,
        trigger=trigger,
        id="snapshot_job",
        replace_existing=True,
    )
    log.info("snapshot scheduler: %s mode, next_run=%s",
             mode, _schedule_next_run_text())


def init_scheduler() -> None:
    """Start the APScheduler background process and load current DB state."""
    global _scheduler
    if _scheduler is not None:
        return
    _scheduler = BackgroundScheduler(timezone="UTC")
    _scheduler.start()
    state_dict = db.get_schedule_state()
    _apply_state(state_dict)
    log.info("snapshot scheduler initialized: %s", state_dict)


def update_schedule(
    enabled: Optional[bool] = None,
    mode: Optional[str] = None,
    interval_min: Optional[int] = None,
    cron_expr: Optional[str] = None,
) -> Dict[str, Any]:
    new_state = db.update_schedule_state(
        enabled=enabled, mode=mode,
        interval_min=interval_min, cron_expr=cron_expr,
    )
    _apply_state(new_state)
    new_state["next_run_at"] = _schedule_next_run_text()
    return new_state


def get_schedule() -> Dict[str, Any]:
    s = db.get_schedule_state()
    s["next_run_at"] = _schedule_next_run_text()
    return s


def shutdown_scheduler() -> None:
    global _scheduler, _job
    if _scheduler is not None:
        try:
            _scheduler.shutdown(wait=False)
        except Exception:
            pass
    _scheduler = None
    _job = None
