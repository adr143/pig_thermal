"use client";

import { useEffect, useState, useCallback } from "react";
import {
  api,
  type SnapshotListItem,
  type SnapshotDetail,
  type ScheduleState,
} from "@/lib/api";
import type { SnapshotEvent } from "@/lib/useStream";

interface Props {
  lastSnapshot: SnapshotEvent | null;
  onTakeSnapshot: () => void;
}

export default function SnapshotPanel({ lastSnapshot, onTakeSnapshot }: Props) {
  const [snapshots, setSnapshots] = useState<SnapshotListItem[]>([]);
  const [schedule, setSchedule] = useState<ScheduleState | null>(null);
  const [expandedId, setExpandedId] = useState<number | null>(null);
  const [expandedDetail, setExpandedDetail] = useState<SnapshotDetail | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [toast, setToast] = useState<string | null>(null);

  const refreshList = useCallback(async () => {
    try {
      const list = await api.listSnapshots(50, 0);
      setSnapshots(list);
    } catch (e) {
      setError(String(e));
    }
  }, []);

  const refreshSchedule = useCallback(async () => {
    try {
      const s = await api.getSchedule();
      setSchedule(s);
    } catch (e) {
      setError(String(e));
    }
  }, []);

  useEffect(() => {
    refreshList();
    refreshSchedule();
  }, [refreshList, refreshSchedule]);

  // When a new snapshot event arrives, refresh the list + show toast
  useEffect(() => {
    if (!lastSnapshot) return;
    setToast(
      `Snapshot #${lastSnapshot.snapshot_id} saved (${lastSnapshot.pig_count} pigs)`
    );
    refreshList();
    const t = setTimeout(() => setToast(null), 3000);
    return () => clearTimeout(t);
  }, [lastSnapshot, refreshList]);

  const onTake = async () => {
    setBusy(true);
    setError(null);
    try {
      onTakeSnapshot();
      // Optimistically refresh after a beat (WS event will trigger another)
      setTimeout(refreshList, 800);
    } finally {
      setBusy(false);
    }
  };

  const onExpand = async (id: number) => {
    if (expandedId === id) {
      setExpandedId(null);
      setExpandedDetail(null);
      return;
    }
    setExpandedId(id);
    try {
      const detail = await api.getSnapshot(id);
      setExpandedDetail(detail);
    } catch (e) {
      setError(String(e));
    }
  };

  const onTempSubmit = async (pigId: number, value: string) => {
    if (!expandedDetail) return;
    const temp = parseFloat(value);
    if (isNaN(temp)) {
      setError("Temperature must be a number");
      return;
    }
    try {
      await api.updatePigTemperature(expandedDetail.id, pigId, temp);
      // Refresh detail
      const detail = await api.getSnapshot(expandedDetail.id);
      setExpandedDetail(detail);
      setToast(`Updated pig #${pigId} temp = ${temp}°C`);
      setTimeout(() => setToast(null), 2500);
    } catch (e) {
      setError(String(e));
    }
  };

  const onDelete = async (id: number) => {
    if (!confirm(`Delete snapshot #${id}?`)) return;
    try {
      await api.deleteSnapshot(id);
      await refreshList();
      if (expandedId === id) {
        setExpandedId(null);
        setExpandedDetail(null);
      }
    } catch (e) {
      setError(String(e));
    }
  };

  const onScheduleChange = async (patch: Partial<ScheduleState>) => {
    try {
      const updated = await api.updateSchedule(patch);
      setSchedule(updated);
    } catch (e) {
      setError(String(e));
    }
  };

  return (
    <div className="card flex flex-col gap-3">
      <div className="flex items-center justify-between">
        <h2 className="font-semibold text-gray-100">Snapshots</h2>
        {toast && (
          <div className="text-xs bg-success/20 text-success px-2 py-1 rounded">
            {toast}
          </div>
        )}
      </div>

      {/* Manual snapshot */}
      <div className="flex items-center gap-2">
        <button
          onClick={onTake}
          disabled={busy}
          className="btn btn-primary disabled:opacity-50"
        >
          {busy ? "Capturing..." : "Take Snapshot Now"}
        </button>
        <span className="text-xs text-gray-500">
          Logs immediately (button source)
        </span>
      </div>

      {/* Schedule controls */}
      <div className="border-t border-slate-800 pt-3">
        <div className="flex items-center justify-between mb-2">
          <h3 className="text-sm font-semibold text-gray-300">Scheduled Capture</h3>
          <label className="flex items-center gap-2 text-xs">
            <input
              type="checkbox"
              checked={schedule?.enabled ?? false}
              onChange={(e) =>
                onScheduleChange({ enabled: e.target.checked })
              }
              className="accent-accent"
            />
            Enabled
          </label>
        </div>
        {schedule && (
          <div className="space-y-2">
            <div className="flex items-center gap-2 text-sm">
              <span className="text-gray-500 w-16">Mode:</span>
              <select
                className="input"
                value={schedule.mode}
                onChange={(e) =>
                  onScheduleChange({ mode: e.target.value as "interval" | "cron" })
                }
              >
                <option value="interval">Interval (every N min)</option>
                <option value="cron">Cron (time of day)</option>
              </select>
            </div>
            {schedule.mode === "interval" ? (
              <div className="flex items-center gap-2 text-sm">
                <span className="text-gray-500 w-16">Every:</span>
                <select
                  className="input"
                  value={schedule.interval_min}
                  onChange={(e) =>
                    onScheduleChange({ interval_min: parseInt(e.target.value) })
                  }
                >
                  <option value={15}>15 min</option>
                  <option value={30}>30 min</option>
                  <option value={60}>1 hour</option>
                  <option value={120}>2 hours</option>
                  <option value={240}>4 hours</option>
                </select>
              </div>
            ) : (
              <div className="flex items-center gap-2 text-sm">
                <span className="text-gray-500 w-16">Cron:</span>
                <input
                  type="text"
                  className="input font-mono flex-1"
                  placeholder="0 8 * * * (min hour day month weekday)"
                  value={schedule.cron_expr}
                  onChange={(e) =>
                    onScheduleChange({ cron_expr: e.target.value })
                  }
                />
              </div>
            )}
            <div className="text-[11px] text-gray-500 font-mono">
              last run: {schedule.last_run_at || "—"}
              <br />
              next run: {schedule.next_run_at || "—"}
            </div>
          </div>
        )}
      </div>

      {error && (
        <div className="text-xs text-danger bg-danger/10 px-2 py-1 rounded">
          {error}
        </div>
      )}

      {/* List */}
      <div className="border-t border-slate-800 pt-3 max-h-[420px] overflow-y-auto">
        <div className="text-[11px] uppercase tracking-wider text-gray-500 mb-2">
          Recent snapshots ({snapshots.length})
        </div>
        {snapshots.length === 0 ? (
          <div className="text-sm text-gray-500">No snapshots yet.</div>
        ) : (
          <ul className="space-y-2">
            {snapshots.map((s) => (
              <li key={s.id} className="bg-panel2 rounded-md">
                <button
                  onClick={() => onExpand(s.id)}
                  className="w-full text-left px-3 py-2 hover:bg-slate-700/30 rounded-md"
                >
                  <div className="flex items-center justify-between">
                    <span className="font-mono text-sm">#{s.id}</span>
                    <span
                      className={`text-[10px] px-2 py-0.5 rounded ${
                        s.source === "button"
                          ? "bg-accent/20 text-accent"
                          : "bg-accent2/20 text-accent2"
                      }`}
                    >
                      {s.source}
                    </span>
                  </div>
                  <div className="text-xs text-gray-400 font-mono mt-0.5">
                    {s.captured_at}
                  </div>
                  <div className="text-xs text-gray-300 mt-0.5">
                    {s.pig_count} pig{s.pig_count !== 1 && "s"}
                    {s.avg_thermal_c != null && (
                      <> · avg {s.avg_thermal_c.toFixed(1)}°C</>
                    )}
                  </div>
                </button>
                {expandedId === s.id && expandedDetail && (
                  <SnapshotDetailBody
                    detail={expandedDetail}
                    onTempSubmit={onTempSubmit}
                    onDelete={() => onDelete(s.id)}
                  />
                )}
              </li>
            ))}
          </ul>
        )}
      </div>
    </div>
  );
}

function SnapshotDetailBody({
  detail,
  onTempSubmit,
  onDelete,
}: {
  detail: SnapshotDetail;
  onTempSubmit: (pigId: number, value: string) => void;
  onDelete: () => void;
}) {
  return (
    <div className="px-3 pb-3 pt-1 border-t border-slate-700/50 mt-1 space-y-2">
      <div className="flex gap-2 my-2">
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img
          src={api.snapshotImageUrl(detail.id)}
          alt={`snapshot ${detail.id}`}
          className="w-1/2 rounded border border-slate-700"
        />
        <div className="flex flex-col gap-1 text-[11px] text-gray-400 flex-1">
          <div>snapshot ID: {detail.id}</div>
          <div>source: {detail.source}</div>
          <div>pig count: {detail.pig_count}</div>
          <div>
            avg thermal:{" "}
            {detail.avg_thermal_c != null
              ? `${detail.avg_thermal_c.toFixed(2)}°C`
              : "—"}
          </div>
          <div className="text-gray-500 break-all">
            file: {detail.snapshot_path}
          </div>
        </div>
      </div>

      {detail.pigs.length === 0 ? (
        <div className="text-xs text-gray-500">No pigs detected.</div>
      ) : (
        <div className="space-y-1">
          <div className="text-[11px] uppercase tracking-wider text-gray-500">
            Pigs — fill temperature manually
          </div>
          <table className="w-full text-xs">
            <thead className="text-gray-500">
              <tr>
                <th className="text-left py-1">#</th>
                <th className="text-left py-1">Class</th>
                <th className="text-left py-1">Conf</th>
                <th className="text-left py-1">Temp (°C)</th>
              </tr>
            </thead>
            <tbody>
              {detail.pigs.map((p, i) => (
                <PigRow
                  key={p.id}
                  idx={i + 1}
                  pig={p}
                  onTempSubmit={onTempSubmit}
                />
              ))}
            </tbody>
          </table>
        </div>
      )}

      <div className="flex justify-end pt-2">
        <button onClick={onDelete} className="btn btn-danger text-xs">
          Delete snapshot
        </button>
      </div>
    </div>
  );
}

function PigRow({
  idx,
  pig,
  onTempSubmit,
}: {
  idx: number;
  pig: SnapshotDetail["pigs"][number];
  onTempSubmit: (pigId: number, value: string) => void;
}) {
  const [val, setVal] = useState(
    pig.temperature_c != null ? String(pig.temperature_c) : ""
  );
  useEffect(() => {
    setVal(pig.temperature_c != null ? String(pig.temperature_c) : "");
  }, [pig.temperature_c]);

  return (
    <tr className="border-t border-slate-700/50">
      <td className="py-1 font-mono">{idx}</td>
      <td className="py-1">{pig.classification || "—"}</td>
      <td className="py-1 font-mono text-gray-400">
        {pig.class_conf != null ? `${(pig.class_conf * 100).toFixed(0)}%` : "—"}
      </td>
      <td className="py-1">
        <div className="flex items-center gap-1">
          <input
            type="number"
            step="0.1"
            className="input w-20"
            placeholder="—"
            value={val}
            onChange={(e) => setVal(e.target.value)}
            onBlur={() => val && onTempSubmit(pig.id, val)}
            onKeyDown={(e) => {
              if (e.key === "Enter") (e.target as HTMLInputElement).blur();
            }}
          />
          <button
            className="btn btn-secondary text-[10px] px-2 py-0.5"
            onClick={() => onTempSubmit(pig.id, val)}
          >
            Save
          </button>
        </div>
      </td>
    </tr>
  );
}
