"use client";

import Link from "next/link";
import { useCallback, useEffect, useState } from "react";
import SnapshotDetailsModal from "@/app/components/SnapshotDetailsModal";
import { api, type SnapshotListItem } from "@/lib/api";

const PAGE_SIZE = 50;

export default function SnapshotsPage() {
  const [snapshots, setSnapshots] = useState<SnapshotListItem[]>([]);
  const [offset, setOffset] = useState(0);
  const [selectedId, setSelectedId] = useState<number | null>(null);
  const [loading, setLoading] = useState(true);
  const [hasNext, setHasNext] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const refreshSnapshots = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      const results = await api.listSnapshots(PAGE_SIZE + 1, offset);
      setSnapshots(results.slice(0, PAGE_SIZE));
      setHasNext(results.length > PAGE_SIZE);
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  }, [offset]);

  useEffect(() => {
    refreshSnapshots();
  }, [refreshSnapshots]);

  const onDeleted = async (snapshotId: number) => {
    setSelectedId(null);
    const remaining = snapshots.filter((snapshot) => snapshot.id !== snapshotId);
    if (remaining.length === 0 && offset > 0) {
      setOffset(Math.max(0, offset - PAGE_SIZE));
      return;
    }
    await refreshSnapshots();
  };

  return (
    <main className="min-h-screen p-4 md:p-6">
      <header className="mb-6 flex flex-wrap items-center justify-between gap-3">
        <div>
          <h1 className="text-xl font-bold text-gray-100 md:text-2xl">
            Snapshot History
          </h1>
          <p className="mt-1 text-sm text-gray-500">
            Browse saved snapshots and open a record for its image and details.
          </p>
        </div>
        <Link href="/" className="btn btn-secondary">
          Back to dashboard
        </Link>
      </header>

      <section className="card">
        {error && (
          <div className="mb-4 rounded bg-danger/10 px-3 py-2 text-sm text-danger">
            {error}
          </div>
        )}
        {loading && snapshots.length === 0 ? (
          <p className="text-sm text-gray-400">Loading snapshots…</p>
        ) : snapshots.length === 0 ? (
          <p className="text-sm text-gray-500">No snapshots recorded yet.</p>
        ) : (
          <>
            <div className="overflow-x-auto">
              <table className="w-full min-w-[640px] text-sm">
                <thead className="text-left text-xs uppercase tracking-wide text-gray-500">
                  <tr>
                    <th className="py-3 pr-4">Snapshot</th>
                    <th className="py-3 pr-4">Captured at</th>
                    <th className="py-3 pr-4">Source</th>
                    <th className="py-3 pr-4">Labeled objects</th>
                    <th className="py-3">Average thermal</th>
                  </tr>
                </thead>
                <tbody>
                  {snapshots.map((snapshot) => (
                    <tr
                      key={snapshot.id}
                      className="cursor-pointer border-t border-slate-800 hover:bg-panel2/70"
                      onClick={() => setSelectedId(snapshot.id)}
                      onKeyDown={(event) => {
                        if (event.key === "Enter" || event.key === " ") {
                          event.preventDefault();
                          setSelectedId(snapshot.id);
                        }
                      }}
                      tabIndex={0}
                      aria-label={`Open snapshot ${snapshot.id}`}
                    >
                      <td className="py-3 pr-4 font-mono">#{snapshot.id}</td>
                      <td className="py-3 pr-4 font-mono text-gray-400">
                        {snapshot.captured_at}
                      </td>
                      <td className="py-3 pr-4 capitalize">{snapshot.source}</td>
                      <td className="py-3 pr-4">{snapshot.pig_count}</td>
                      <td className="py-3">
                        {snapshot.avg_thermal_c == null
                          ? "—"
                          : `${snapshot.avg_thermal_c.toFixed(2)}°C`}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            <div className="mt-4 flex items-center justify-between border-t border-slate-800 pt-4">
              <button
                type="button"
                className="btn btn-secondary disabled:cursor-not-allowed disabled:opacity-40"
                disabled={offset === 0 || loading}
                onClick={() => setOffset(Math.max(0, offset - PAGE_SIZE))}
              >
                Previous
              </button>
              <span className="text-xs text-gray-500">
                Showing {offset + 1}–{offset + snapshots.length}
              </span>
              <button
                type="button"
                className="btn btn-secondary disabled:cursor-not-allowed disabled:opacity-40"
                disabled={!hasNext || loading}
                onClick={() => setOffset(offset + PAGE_SIZE)}
              >
                Next
              </button>
            </div>
          </>
        )}
      </section>

      {selectedId !== null && (
        <SnapshotDetailsModal
          snapshotId={selectedId}
          onClose={() => setSelectedId(null)}
          onDeleted={onDeleted}
        />
      )}
    </main>
  );
}
