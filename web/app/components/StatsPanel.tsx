"use client";

import type { WebcamStats } from "@/lib/useStream";

interface Props {
  stats: WebcamStats | null;
  thermalClients: number;
  browserClients: number;
  thermalAgeS: number;
  avgThermalC: number | null;
}

function Stat({
  label,
  value,
  hint,
}: {
  label: string;
  value: string | number;
  hint?: string;
}) {
  return (
    <div className="bg-panel2 rounded-md px-3 py-2">
      <div className="text-[11px] uppercase tracking-wider text-gray-500">
        {label}
      </div>
      <div className="text-xl font-semibold text-gray-100 font-mono">
        {value}
      </div>
      {hint && <div className="text-[11px] text-gray-500">{hint}</div>}
    </div>
  );
}

export default function StatsPanel({
  stats,
  thermalClients,
  browserClients,
  thermalAgeS,
  avgThermalC,
}: Props) {
  const pigCount = stats?.pig_count ?? 0;
  const classes = stats?.classifications ?? {};
  const modelsLoaded = stats?.models_loaded ?? false;
  const avgThermal =
    avgThermalC != null ? avgThermalC : stats?.avg_thermal_c ?? null;

  return (
    <div className="card flex flex-col gap-3">
      <div className="flex items-center justify-between">
        <h2 className="font-semibold text-gray-100">Live Stats</h2>
        <div className="text-[11px] flex items-center gap-2">
          <span
            className={`px-2 py-0.5 rounded ${
              modelsLoaded ? "bg-success/20 text-success" : "bg-danger/20 text-danger"
            }`}
          >
            {modelsLoaded ? "models OK" : "no models"}
          </span>
        </div>
      </div>

      <div className="grid grid-cols-3 gap-2">
        <Stat
          label="Pigs detected"
          value={pigCount}
          hint="from webcam YOLO"
        />
        <Stat
          label="Avg thermal"
          value={avgThermal != null ? `${avgThermal.toFixed(1)}°C` : "--"}
          hint="pig body proxy"
        />
        <Stat
          label="Class types"
          value={Object.keys(classes).length}
          hint="distinct classes"
        />
      </div>

      <div>
        <div className="text-[11px] uppercase tracking-wider text-gray-500 mb-1">
          Classification breakdown
        </div>
        {Object.keys(classes).length === 0 ? (
          <div className="text-sm text-gray-500">No classifications yet.</div>
        ) : (
          <ul className="space-y-1">
            {Object.entries(classes)
              .sort((a, b) => b[1] - a[1])
              .map(([cls, count]) => (
                <li
                  key={cls}
                  className="flex items-center justify-between bg-panel2 rounded px-2 py-1 text-sm"
                >
                  <span className="text-gray-200">{cls}</span>
                  <span className="font-mono text-accent2">{count}</span>
                </li>
              ))}
          </ul>
        )}
      </div>

      <div className="text-[11px] text-gray-500 border-t border-slate-800 pt-2 space-y-0.5 font-mono">
        <div>thermal clients: {thermalClients}</div>
        <div>browser clients: {browserClients}</div>
        <div>
          thermal last seen:{" "}
          {thermalAgeS > 1000 ? "never" : `${thermalAgeS.toFixed(0)}s ago`}
        </div>
      </div>
    </div>
  );
}
