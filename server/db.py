from __future__ import annotations

import os
import sqlite3
import threading
import time
from contextlib import contextmanager
from datetime import datetime
from typing import Any, Dict, List, Optional

from config import config

_lock = threading.Lock()


def _db_path() -> str:
    return config.DB_PATH


def get_conn() -> sqlite3.Connection:
    conn = sqlite3.connect(_db_path(), check_same_thread=False, timeout=10.0)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA foreign_keys=ON;")
    return conn


@contextmanager
def tx():
    conn = get_conn()
    try:
        with _lock:
            yield conn
            conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS snapshots (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    captured_at   TEXT    NOT NULL,
    source        TEXT    NOT NULL,
    pig_count     INTEGER NOT NULL DEFAULT 0,
    avg_thermal_c REAL,
    snapshot_path TEXT,
    notes         TEXT
);

CREATE TABLE IF NOT EXISTS pigs (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id    INTEGER NOT NULL,
    bbox_x1        INTEGER,
    bbox_y1        INTEGER,
    bbox_x2        INTEGER,
    bbox_y2        INTEGER,
    detect_conf    REAL,
    classification TEXT,
    class_conf     REAL,
    temperature_c  REAL,
    created_at     TEXT NOT NULL,
    updated_at     TEXT NOT NULL,
    FOREIGN KEY (snapshot_id) REFERENCES snapshots(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_pigs_snapshot ON pigs(snapshot_id);

CREATE TABLE IF NOT EXISTS schedule_state (
    id            INTEGER PRIMARY KEY CHECK (id = 1),
    enabled       INTEGER NOT NULL DEFAULT 0,
    mode          TEXT    NOT NULL DEFAULT 'interval',
    interval_min  INTEGER NOT NULL DEFAULT 15,
    cron_expr     TEXT    NOT NULL DEFAULT '0 8 * * *',
    last_run_at   TEXT,
    next_run_at   TEXT
);
"""


def init_db() -> None:
    os.makedirs(os.path.dirname(os.path.abspath(_db_path())), exist_ok=True)
    with tx() as conn:
        conn.executescript(SCHEMA_SQL)


        conn.execute(
            """INSERT OR IGNORE INTO schedule_state
               (id, enabled, mode, interval_min, cron_expr)
               VALUES (1, ?, ?, ?, ?)""",
            (
                int(config.DEFAULT_SCHEDULE_ENABLED),
                config.DEFAULT_SCHEDULE_MODE,
                config.DEFAULT_SCHEDULE_INTERVAL_MIN,
                config.DEFAULT_SCHEDULE_CRON_EXPR,
            ),
        )


def _now_iso() -> str:
    return datetime.utcnow().isoformat(timespec="seconds") + "Z"


def insert_snapshot(
    captured_at: str,
    source: str,
    pig_count: int,
    avg_thermal_c: Optional[float],
    snapshot_path: str,
    pigs: List[Dict[str, Any]],
) -> int:
    with tx() as conn:
        cur = conn.execute(
            """INSERT INTO snapshots
               (captured_at, source, pig_count, avg_thermal_c, snapshot_path, notes)
               VALUES (?, ?, ?, ?, ?, NULL)""",
            (captured_at, source, pig_count, avg_thermal_c, snapshot_path),
        )
        snapshot_id = cur.lastrowid
        now = _now_iso()
        for p in pigs:
            conn.execute(
                """INSERT INTO pigs
                   (snapshot_id, bbox_x1, bbox_y1, bbox_x2, bbox_y2,
                    detect_conf, classification, class_conf,
                    temperature_c, created_at, updated_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?)""",
                (
                    snapshot_id,
                    p.get("bbox_x1"), p.get("bbox_y1"),
                    p.get("bbox_x2"), p.get("bbox_y2"),
                    p.get("detect_conf"),
                    p.get("classification"),
                    p.get("class_conf"),
                    now, now,
                ),
            )
        return snapshot_id


def list_snapshots(limit: int = 100, offset: int = 0) -> List[Dict[str, Any]]:
    with tx() as conn:
        rows = conn.execute(
            """SELECT id, captured_at, source, pig_count, avg_thermal_c,
                      snapshot_path, notes
               FROM snapshots
               ORDER BY id DESC
               LIMIT ? OFFSET ?""",
            (limit, offset),
        ).fetchall()
        return [dict(r) for r in rows]


def get_snapshot(snapshot_id: int) -> Optional[Dict[str, Any]]:
    with tx() as conn:
        snap = conn.execute(
            """SELECT * FROM snapshots WHERE id = ?""",
            (snapshot_id,),
        ).fetchone()
        if snap is None:
            return None
        pigs = conn.execute(
            """SELECT * FROM pigs WHERE snapshot_id = ? ORDER BY id""",
            (snapshot_id,),
        ).fetchall()
        out = dict(snap)
        out["pigs"] = [dict(p) for p in pigs]
        return out


def update_pig_temperature(pig_id: int, temperature_c: float) -> bool:
    with tx() as conn:
        cur = conn.execute(
            "UPDATE pigs SET temperature_c = ?, updated_at = ? WHERE id = ?",
            (temperature_c, _now_iso(), pig_id),
        )
        return cur.rowcount > 0


def delete_snapshot(snapshot_id: int) -> bool:
    with tx() as conn:
        cur = conn.execute(
            "DELETE FROM snapshots WHERE id = ?", (snapshot_id,)
        )
        return cur.rowcount > 0


def get_schedule_state() -> Dict[str, Any]:
    with tx() as conn:
        row = conn.execute(
            "SELECT * FROM schedule_state WHERE id = 1"
        ).fetchone()
        return dict(row) if row else {}


def update_schedule_state(
    enabled: Optional[bool] = None,
    mode: Optional[str] = None,
    interval_min: Optional[int] = None,
    cron_expr: Optional[str] = None,
) -> Dict[str, Any]:
    fields = []
    vals: list = []
    if enabled is not None:
        fields.append("enabled = ?"); vals.append(int(enabled))
    if mode is not None:
        fields.append("mode = ?");     vals.append(mode)
    if interval_min is not None:
        fields.append("interval_min = ?"); vals.append(interval_min)
    if cron_expr is not None:
        fields.append("cron_expr = ?"); vals.append(cron_expr)
    if not fields:
        return get_schedule_state()
    with tx() as conn:
        conn.execute(
            f"UPDATE schedule_state SET {', '.join(fields)} WHERE id = 1",
            vals,
        )
    return get_schedule_state()


def mark_schedule_run(now_iso: Optional[str] = None, next_iso: Optional[str] = None):
    fields = ["last_run_at = ?"]; vals: list = [now_iso]
    if next_iso:
        fields.append("next_run_at = ?"); vals.append(next_iso)
    with tx() as conn:
        conn.execute(
            f"UPDATE schedule_state SET {', '.join(fields)} WHERE id = 1", vals
        )
