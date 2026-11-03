"use client";

import Link from "next/link";
import { useCallback, useEffect, useState } from "react";
import {
  api,
  type SnapshotListItem,
  type ScheduleState,
} from "@/lib/api";
import type { SnapshotEvent } from "@/lib/useStream";

interface Props {
  lastSnapshot: SnapshotEvent | null;
  onTakeSnapshot: () => void;
}

export default function SnapshotPanel({
  lastSnapshot,
  onTakeSnapshot,
}: Props) {
  const [latest, setLatest] = useState<SnapshotListItem | null>(null);
  const [schedule, setSchedule] = useState<ScheduleState | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refreshLatest = useCallback(async () => {
    try {
      const [snapshots, currentSchedule] = await Promise.all([
        api.listSnapshots(1, 0),
        api.getSchedule(),
      ]);
      setLatest(snapshots[0] ?? null);
      setSchedule(currentSchedule);
    } catch (e) {
      setError(String(e));
    }
  }, []);

  useEffect(() => {
    refreshLatest();
  }, [refreshLatest]);

  useEffect(() => {
    if (!lastSnapshot) return;
    setLatest({
      id: lastSnapshot.snapshot_id,
      captured_at: lastSnapshot.captured_at,
      source: lastSnapshot.source,
      pig_count: lastSnapshot.pig_count,
      avg_thermal_c: lastSnapshot.avg_thermal_c,
      snapshot_path: lastSnapshot.snapshot_path,
      notes: null,
    });
  }, [lastSnapshot]);

  const takeSnapshot = async () => {
    setBusy(true);
    setError(null);
    try {
      onTakeSnapshot();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  };

  const onScheduleChange = async (patch: Partial<ScheduleState>) => {
    setError(null);
    try {
      setSchedule(await api.updateSchedule(patch));
    } catch (e) {
      setError(String(e));
    }
  };

  return (
    <section className="card flex flex-col gap-3">
      <div className="flex items-center justify-between gap-3">
        <h2 className="font-semibold text-gray-100">Latest Snapshot</h2>
        <Link href="/snapshots" className="text-sm text-accent hover:underline">
          Snapshot history
        </Link>
      </div>

      <button
        onClick={takeSnapshot}
        disabled={busy}
        className="btn btn-primary self-start disabled:opacity-50"
      >
        {busy ? "Capturing..." : "Take Snapshot Now"}
      </button>

      {latest ? (
        <div className="rounded-md bg-panel2 p-3">
          <div className="flex flex-wrap items-center justify-between gap-2">
            <span className="font-mono text-sm">Snapshot #{latest.id}</span>
            <span className="text-xs text-gray-400">{latest.source}</span>
          </div>
          <div className="mt-1 text-xs text-gray-400 font-mono">
            {latest.captured_at}
          </div>
          <div className="mt-1 text-sm text-gray-200">
            {latest.pig_count} labeled object{latest.pig_count !== 1 && "s"}
            {latest.avg_thermal_c != null && (
              <> · {latest.avg_thermal_c.toFixed(1)}°C average</>
            )}
          </div>
          {latest.snapshot_path && (
            // eslint-disable-next-line @next/next/no-img-element
            <img
              src={api.snapshotImageUrl(latest.id)}
              alt={`Latest snapshot ${latest.id}`}
              className="mt-3 max-h-64 w-full rounded border border-slate-700 object-contain"
            />
          )}
          <Link
            href="/snapshots"
            className="mt-2 inline-block text-xs text-accent hover:underline"
          >
            View snapshot details
          </Link>
        </div>
      ) : (
        <p className="text-sm text-gray-500">No snapshots recorded yet.</p>
      )}

      <div className="border-t border-slate-800 pt-3">
        <div className="mb-2 flex items-center justify-between">
          <h3 className="text-sm font-semibold text-gray-300">
            Scheduled Capture
          </h3>
          <label className="flex items-center gap-2 text-xs">
            <input
              type="checkbox"
              checked={schedule?.enabled ?? false}
              onChange={(e) => onScheduleChange({ enabled: e.target.checked })}
              className="accent-accent"
            />
            Enabled
          </label>
        </div>
        {schedule && (
          <div className="space-y-2">
            <div className="flex items-center gap-2 text-sm">
              <span className="w-16 text-gray-500">Mode:</span>
              <select
                className="input"
                value={schedule.mode}
                onChange={(e) =>
                  onScheduleChange({
                    mode: e.target.value as "interval" | "cron",
                  })
                }
              >
                <option value="interval">Interval</option>
                <option value="cron">Cron</option>
              </select>
            </div>
            {schedule.mode === "interval" ? (
              <div className="flex items-center gap-2 text-sm">
                <span className="w-16 text-gray-500">Every:</span>
                <select
                  className="input"
                  value={schedule.interval_min}
                  onChange={(e) =>
                    onScheduleChange({
                      interval_min: parseInt(e.target.value, 10),
                    })
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
                <span className="w-16 text-gray-500">Cron:</span>
                <input
                  className="input min-w-0 flex-1 font-mono"
                  placeholder="0 8 * * *"
                  value={schedule.cron_expr}
                  onChange={(e) =>
                    onScheduleChange({ cron_expr: e.target.value })
                  }
                />
              </div>
            )}
            <div className="text-[11px] font-mono text-gray-500">
              Last run: {schedule.last_run_at || "—"}
              <br />
              Next run: {schedule.next_run_at || "—"}
            </div>
          </div>
        )}
      </div>

      {error && (
        <div className="rounded bg-danger/10 px-2 py-1 text-xs text-danger">
          {error}
          <button
            className="ml-2 underline"
            onClick={() => setError(null)}
            aria-label="Dismiss error"
          >
            Dismiss
          </button>
        </div>
      )}
    </section>
  );
}
