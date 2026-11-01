"use client";

import { useEffect, useState } from "react";
import { useStream } from "@/lib/useStream";
import WebcamView from "./components/WebcamView";
import ThermalView from "./components/ThermalView";
import StatsPanel from "./components/StatsPanel";
import SnapshotPanel from "./components/SnapshotPanel";

type ViewMode = "webcam" | "thermal" | "split";

export default function Page() {
  const stream = useStream();
  const [view, setView] = useState<ViewMode>("webcam");

  // Use serverInfo or live stats; compute displayable fields
  const thermalClients = stream.serverInfo?.thermal_clients ?? 0;
  const browserClients = stream.serverInfo?.browser_clients ?? 0;

  // Track the last thermal message timestamp so we can show "X s ago"
  const [thermalTs, setThermalTs] = useState<number | null>(null);
  useEffect(() => {
    if (stream.thermal) setThermalTs(Date.now());
  }, [stream.thermal]);

  // Re-render every second so the "thermal age" indicator updates
  const [, setNowTick] = useState(0);
  useEffect(() => {
    const t = setInterval(() => setNowTick((n) => n + 1), 1000);
    return () => clearInterval(t);
  }, []);

  const thermalAgeDisplay = thermalTs ? (Date.now() - thermalTs) / 1000 : 1000;

  return (
    <main className="min-h-screen p-4 md:p-6">
      {/* Header */}
      <header className="flex items-center justify-between mb-4">
        <div>
          <h1 className="text-xl md:text-2xl font-bold text-gray-100">
            Pig Monitoring System
          </h1>
          <p className="text-xs text-gray-500">
            Webcam + thermal · pig detection (YOLO) + classification ·
            scheduled + manual snapshots
          </p>
        </div>
        <div className="flex items-center gap-2 text-xs">
          <span
            className={`px-2 py-1 rounded font-mono ${
              stream.connected
                ? "bg-success/20 text-success"
                : "bg-danger/20 text-danger"
            }`}
          >
            {stream.connected ? "● live" : "○ disconnected"}
          </span>
        </div>
      </header>

      <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
        {/* Left: live views */}
        <div className="lg:col-span-2 space-y-4">
          {/* Tab bar */}
          <div className="flex items-center gap-1 bg-panel border border-slate-800 rounded-md p-1">
            <TabButton
              active={view === "webcam"}
              onClick={() => setView("webcam")}
            >
              Webcam
            </TabButton>
            <TabButton
              active={view === "thermal"}
              onClick={() => setView("thermal")}
            >
              Thermal
            </TabButton>
            <div className="flex-1" />
            <label className="flex items-center gap-2 text-xs px-3 py-1.5 cursor-pointer">
              <input
                type="checkbox"
                checked={view === "split"}
                onChange={(e) =>
                  setView(e.target.checked ? "split" : "webcam")
                }
                className="accent-accent"
              />
              Side-by-side
            </label>
          </div>

          {/* Live view */}
          {view === "webcam" && (
            <div className="card">
              <h2 className="font-semibold text-gray-100 mb-3">Webcam Live</h2>
              <WebcamView
                img={stream.webcamImg}
                detections={stream.webcamDetections}
              />
            </div>
          )}

          {view === "thermal" && (
            <div className="card">
              <h2 className="font-semibold text-gray-100 mb-3">
                Thermal Live (MLX90640 32×24)
              </h2>
              {stream.thermal ? (
                <ThermalView
                  temps={stream.thermal.temps}
                  ambient={stream.thermal.ambient}
                  stats={stream.thermal.stats}
                />
              ) : (
                <div className="aspect-video flex items-center justify-center bg-black rounded text-gray-500">
                  Waiting for thermal stream from ESP32...
                </div>
              )}
            </div>
          )}

          {view === "split" && (
            <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
              <div className="card">
                <h2 className="font-semibold text-gray-100 mb-3">Webcam</h2>
                <WebcamView
                  img={stream.webcamImg}
                  detections={stream.webcamDetections}
                />
              </div>
              <div className="card">
                <h2 className="font-semibold text-gray-100 mb-3">Thermal</h2>
                {stream.thermal ? (
                  <ThermalView
                    temps={stream.thermal.temps}
                    ambient={stream.thermal.ambient}
                    stats={stream.thermal.stats}
                  />
                ) : (
                  <div className="aspect-video flex items-center justify-center bg-black rounded text-gray-500">
                    Waiting for thermal stream...
                  </div>
                )}
              </div>
            </div>
          )}

          {/* Snapshot preview toast when triggered */}
          {stream.lastSnapshot && (
            <div className="card border-accent/40">
              <div className="flex items-start gap-3">
                <div className="flex-1">
                  <div className="font-semibold text-gray-100">
                    Latest snapshot #{stream.lastSnapshot.snapshot_id}
                  </div>
                  <div className="text-xs text-gray-400 font-mono">
                    {stream.lastSnapshot.captured_at} ·{" "}
                    {stream.lastSnapshot.source} ·{" "}
                    {stream.lastSnapshot.pig_count} pigs
                  </div>
                </div>
              </div>
            </div>
          )}
        </div>

        {/* Right: stats + snapshot panel */}
        <div className="space-y-4">
          <StatsPanel
            stats={stream.webcamStats}
            thermalClients={thermalClients}
            browserClients={browserClients}
            thermalAgeS={thermalAgeDisplay}
            avgThermalC={
              stream.thermal?.stats?.avg_hot_c ??
              stream.thermal?.stats?.avg_all_c ??
              null
            }
          />
          <SnapshotPanel
            lastSnapshot={stream.lastSnapshot}
            onTakeSnapshot={stream.takeSnapshot}
          />
        </div>
      </div>

      <footer className="mt-6 text-center text-xs text-gray-600">
        ESP32 thermal → Flask WebSocket → YOLO inference → SQLite · Next.js UI
      </footer>
    </main>
  );
}

function TabButton({
  active,
  onClick,
  children,
}: {
  active: boolean;
  onClick: () => void;
  children: React.ReactNode;
}) {
  return (
    <button
      onClick={onClick}
      className={`px-4 py-1.5 rounded text-sm font-medium transition-colors ${
        active
          ? "bg-accent text-black"
          : "text-gray-300 hover:bg-panel2"
      }`}
    >
      {children}
    </button>
  );
}
