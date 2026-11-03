"use client";

import { useCallback, useEffect, useState } from "react";
import { api, type SnapshotDetail } from "@/lib/api";

interface Props {
  snapshotId: number;
  onClose: () => void;
  onDeleted: (snapshotId: number) => Promise<void>;
}

export default function SnapshotDetailsModal({
  snapshotId,
  onClose,
  onDeleted,
}: Props) {
  const [detail, setDetail] = useState<SnapshotDetail | null>(null);
  const [loading, setLoading] = useState(true);
  const [working, setWorking] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refreshDetail = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      setDetail(await api.getSnapshot(snapshotId));
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  }, [snapshotId]);

  useEffect(() => {
    refreshDetail();
  }, [refreshDetail]);

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") onClose();
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [onClose]);

  const saveTemperature = async (pigId: number, value: string) => {
    const temperature = Number(value);
    if (!value.trim() || !Number.isFinite(temperature)) {
      setError("Temperature must be a number");
      return;
    }
    setWorking(true);
    setError(null);
    try {
      await api.updatePigTemperature(snapshotId, pigId, temperature);
      await refreshDetail();
    } catch (e) {
      setError(String(e));
    } finally {
      setWorking(false);
    }
  };

  const deleteSnapshot = async () => {
    if (!confirm(`Delete snapshot #${snapshotId}?`)) return;
    setWorking(true);
    setError(null);
    try {
      await api.deleteSnapshot(snapshotId);
      await onDeleted(snapshotId);
    } catch (e) {
      setError(String(e));
      setWorking(false);
    }
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4"
      role="presentation"
      onMouseDown={(event) => {
        if (event.target === event.currentTarget) onClose();
      }}
    >
      <section
        role="dialog"
        aria-modal="true"
        aria-labelledby="snapshot-dialog-title"
        className="card max-h-[90vh] w-full max-w-4xl overflow-y-auto shadow-xl"
      >
        <div className="mb-4 flex items-center justify-between gap-4">
          <h2 id="snapshot-dialog-title" className="text-lg font-semibold">
            Snapshot #{snapshotId}
          </h2>
          <button
            type="button"
            className="btn btn-secondary"
            onClick={onClose}
            aria-label="Close snapshot details"
          >
            Close
          </button>
        </div>

        {loading && <p className="text-sm text-gray-400">Loading snapshot…</p>}
        {!loading && error && (
          <div className="mb-3 rounded bg-danger/10 px-3 py-2 text-sm text-danger">
            {error}
          </div>
        )}
        {detail && (
          <>
            <div className="grid gap-4 md:grid-cols-2">
              <div className="flex min-h-40 items-center justify-center overflow-hidden rounded border border-slate-700 bg-black">
                {/* eslint-disable-next-line @next/next/no-img-element */}
                <img
                  src={api.snapshotImageUrl(detail.id)}
                  alt={`Snapshot ${detail.id}`}
                  className="h-auto max-h-[55vh] w-full object-contain"
                />
              </div>
              <dl className="grid content-start grid-cols-[auto_1fr] gap-x-4 gap-y-2 text-sm">
                <dt className="text-gray-500">Captured</dt>
                <dd className="font-mono">{detail.captured_at}</dd>
                <dt className="text-gray-500">Source</dt>
                <dd>{detail.source}</dd>
                <dt className="text-gray-500">Labeled objects</dt>
                <dd>{detail.pig_count}</dd>
                <dt className="text-gray-500">Average thermal</dt>
                <dd>
                  {detail.avg_thermal_c == null
                    ? "—"
                    : `${detail.avg_thermal_c.toFixed(2)}°C`}
                </dd>
              </dl>
            </div>

            <div className="mt-5">
              <h3 className="mb-2 text-sm font-semibold text-gray-300">
                Detected objects and temperatures
              </h3>
              {detail.pigs.length === 0 ? (
                <p className="text-sm text-gray-500">No labeled objects.</p>
              ) : (
                <div className="overflow-x-auto">
                  <table className="w-full min-w-[560px] text-sm">
                    <thead className="text-left text-xs text-gray-500">
                      <tr>
                        <th className="py-2 pr-3">#</th>
                        <th className="py-2 pr-3">Class</th>
                        <th className="py-2 pr-3">Class confidence</th>
                        <th className="py-2 pr-3">Detection confidence</th>
                        <th className="py-2">Temperature (°C)</th>
                      </tr>
                    </thead>
                    <tbody>
                      {detail.pigs.map((pig, index) => (
                        <PigTemperatureRow
                          key={pig.id}
                          index={index + 1}
                          pig={pig}
                          disabled={working}
                          onSave={saveTemperature}
                        />
                      ))}
                    </tbody>
                  </table>
                </div>
              )}
            </div>

            <div className="mt-5 flex justify-end">
              <button
                type="button"
                className="btn btn-danger disabled:opacity-50"
                onClick={deleteSnapshot}
                disabled={working}
              >
                Delete snapshot
              </button>
            </div>
          </>
        )}
      </section>
    </div>
  );
}

function PigTemperatureRow({
  index,
  pig,
  disabled,
  onSave,
}: {
  index: number;
  pig: SnapshotDetail["pigs"][number];
  disabled: boolean;
  onSave: (pigId: number, value: string) => void;
}) {
  const [value, setValue] = useState(
    pig.temperature_c == null ? "" : String(pig.temperature_c)
  );

  useEffect(() => {
    setValue(pig.temperature_c == null ? "" : String(pig.temperature_c));
  }, [pig.temperature_c]);

  return (
    <tr className="border-t border-slate-700/50">
      <td className="py-2 pr-3 font-mono">{index}</td>
      <td className="py-2 pr-3">{pig.classification || "—"}</td>
      <td className="py-2 pr-3">
        {pig.class_conf == null ? "—" : `${(pig.class_conf * 100).toFixed(0)}%`}
      </td>
      <td className="py-2 pr-3">
        {pig.detect_conf == null ? "—" : `${(pig.detect_conf * 100).toFixed(0)}%`}
      </td>
      <td className="py-2">
        <div className="flex items-center gap-2">
          <input
            type="number"
            step="0.1"
            className="input w-24"
            value={value}
            onChange={(event) => setValue(event.target.value)}
            aria-label={`Temperature for object ${index}`}
          />
          <button
            type="button"
            className="btn btn-secondary px-2 py-1 text-xs disabled:opacity-50"
            disabled={disabled || !value.trim()}
            onClick={() => onSave(pig.id, value)}
          >
            Save
          </button>
        </div>
      </td>
    </tr>
  );
}
