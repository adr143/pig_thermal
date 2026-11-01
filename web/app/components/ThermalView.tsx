"use client";

import { useEffect, useRef } from "react";
import type { ThermalStats } from "@/lib/useStream";

interface Props {
  temps: number[];
  ambient: number;
  stats: ThermalStats | null;
  width?: number;  // thermal grid width (32 for MLX90640)
  height?: number; // thermal grid height (24 for MLX90640)
}

// Simple jet-like colormap (blue->cyan->green->yellow->red)
function colormap(t: number): [number, number, number] {
  // t in [0,1]
  t = Math.max(0, Math.min(1, t));
  const r = Math.min(255, Math.max(0, 255 * (1.5 - Math.abs(4 * t - 3))));
  const g = Math.min(255, Math.max(0, 255 * (1.5 - Math.abs(4 * t - 2))));
  const b = Math.min(255, Math.max(0, 255 * (1.5 - Math.abs(4 * t - 1))));
  return [r | 0, g | 0, b | 0];
}

export default function ThermalView({
  temps,
  ambient,
  stats,
  width = 32,
  height = 24,
}: Props) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    if (!temps || temps.length !== width * height) {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      return;
    }

    // Determine dynamic min/max for colormap normalization
    const lo = stats?.min_c ?? Math.min(...temps);
    const hi = stats?.max_c ?? Math.max(...temps);
    const range = Math.max(1, hi - lo);

    // Render scaled up (32x24 -> 320x240)
    const cellW = canvas.width / width;
    const cellH = canvas.height / height;
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const v = temps[y * width + x];
        const t = (v - lo) / range;
        const [r, g, b] = colormap(t);
        ctx.fillStyle = `rgb(${r},${g},${b})`;
        ctx.fillRect(x * cellW, y * cellH, cellW + 1, cellH + 1);
      }
    }

    // Optional: draw value labels every few cells
    ctx.font = "8px monospace";
    ctx.fillStyle = "rgba(0,0,0,0.6)";
    for (let y = 0; y < height; y += 4) {
      for (let x = 0; x < width; x += 4) {
        const v = temps[y * width + x];
        const t = `${v.toFixed(0)}`;
        ctx.fillText(t, x * cellW + 2, y * cellH + 9);
      }
    }
  }, [temps, stats, width, height]);

  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-center justify-between text-sm">
        <span className="text-gray-400">
          Ambient:{" "}
          <span className="text-accent2 font-mono">
            {ambient != null ? `${ambient.toFixed(1)}°C` : "--"}
          </span>
        </span>
        {stats && (
          <span className="text-gray-400 font-mono text-xs">
            avg {stats.avg_all_c.toFixed(1)}°C · max {stats.max_c.toFixed(1)}°C ·
            min {stats.min_c.toFixed(1)}°C
            {stats.avg_hot_c != null && (
              <> · hot {stats.avg_hot_c.toFixed(1)}°C</>
            )}
          </span>
        )}
      </div>
      <canvas
        ref={canvasRef}
        width={320}
        height={240}
        className="w-full rounded-md bg-black border border-slate-800"
        style={{ aspectRatio: "32 / 24", imageRendering: "pixelated" }}
      />
      {/* Color legend */}
      <div
        className="h-3 w-full rounded"
        style={{
          background:
            "linear-gradient(to right, rgb(0,0,255), rgb(0,255,255), rgb(0,255,0), rgb(255,255,0), rgb(255,0,0))",
        }}
      />
      <div className="flex justify-between text-xs text-gray-500 font-mono">
        <span>cold</span>
        <span>hot</span>
      </div>
    </div>
  );
}
