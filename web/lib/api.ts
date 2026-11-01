// REST API client for the Flask backend.
// In dev, requests go through the Next.js rewrite proxy (next.config.js)
// so we can use relative URLs. In production, set NEXT_PUBLIC_API_BASE.

const API_BASE = process.env.NEXT_PUBLIC_API_BASE || "";

export interface SnapshotListItem {
  id: number;
  captured_at: string;
  source: "button" | "schedule";
  pig_count: number;
  avg_thermal_c: number | null;
  snapshot_path: string;
  notes: string | null;
}

export interface PigRecord {
  id: number;
  snapshot_id: number;
  bbox_x1: number | null;
  bbox_y1: number | null;
  bbox_x2: number | null;
  bbox_y2: number | null;
  detect_conf: number | null;
  classification: string | null;
  class_conf: number | null;
  temperature_c: number | null;
  created_at: string;
  updated_at: string;
}

export interface SnapshotDetail extends SnapshotListItem {
  pigs: PigRecord[];
}

export interface ScheduleState {
  enabled: boolean;
  mode: "interval" | "cron";
  interval_min: number;
  cron_expr: string;
  last_run_at: string | null;
  next_run_at: string | null;
}

async function req<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, {
    ...init,
    headers: {
      "Content-Type": "application/json",
      ...(init?.headers || {}),
    },
    cache: "no-store",
  });
  if (!res.ok) {
    const text = await res.text().catch(() => "");
    throw new Error(`${res.status} ${res.statusText} ${text}`);
  }
  return res.json() as Promise<T>;
}

export const api = {
  status: () => req<any>("/api/status"),

  listSnapshots: (limit = 100, offset = 0) =>
    req<SnapshotListItem[]>(
      `/api/snapshots?limit=${limit}&offset=${offset}`
    ),

  getSnapshot: (id: number) =>
    req<SnapshotDetail>(`/api/snapshots/${id}`),

  deleteSnapshot: (id: number) =>
    req<{ ok: boolean; deleted: number }>(`/api/snapshots/${id}`, {
      method: "DELETE",
    }),

  takeSnapshot: () =>
    req<any>("/api/snapshots/take", { method: "POST" }),

  updatePigTemperature: (snapshotId: number, pigId: number, temp: number) =>
    req<{ ok: boolean; pig_id: number; temperature_c: number }>(
      `/api/snapshots/${snapshotId}/pigs/${pigId}`,
      {
        method: "PUT",
        body: JSON.stringify({ temperature_c: temp }),
      }
    ),

  snapshotImageUrl: (id: number) =>
    `${API_BASE}/api/snapshots/${id}/image`,

  getSchedule: () => req<ScheduleState>("/api/schedule"),

  updateSchedule: (patch: Partial<ScheduleState>) =>
    req<ScheduleState>("/api/schedule", {
      method: "PUT",
      body: JSON.stringify(patch),
    }),
};

// WebSocket URL helper. In dev, Next.js rewrites /ws/* to the backend.
export function streamWsUrl(): string {
  if (typeof window === "undefined") return "";
  // Use the same host as the page so the rewrite proxy works.
  const proto = window.location.protocol === "https:" ? "wss" : "ws";
  return `${proto}://${window.location.host}/ws/stream`;
}
