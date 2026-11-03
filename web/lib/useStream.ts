"use client";

import { useEffect, useRef, useState, useCallback } from "react";
import { streamWsUrl } from "./api";

export interface WebcamStats {
  pig_count: number;
  classifications: Record<string, number>;
  avg_thermal_c: number | null;
  models_loaded: boolean;
}

export interface ThermalStats {
  avg_all_c: number;
  avg_hot_c: number | null;
  max_c: number;
  min_c: number;
}

export interface WebcamDetection {
  bbox: [number, number, number, number];
  conf: number;
  cls: string | null;
  cls_conf: number | null;
}

export interface SnapshotEvent {
  ok: boolean;
  snapshot_id: number;
  captured_at: string;
  source: "button" | "schedule";
  pig_count: number;
  avg_thermal_c: number | null;
  snapshot_path: string;
  pigs: {
    id: number | null;
    classification: string | null;
    class_conf: number | null;
    detect_conf: number;
    temperature_c: number | null;
    bbox: [number, number, number, number];
  }[];
}

export interface StreamState {
  connected: boolean;
  webcamImg: string | null;
  webcamStats: WebcamStats | null;
  webcamDetections: WebcamDetection[] | null;
  thermal: {
    temps: number[];
    ambient: number;
    stats: ThermalStats | null;
  } | null;
  serverInfo: { thermal_clients: number; browser_clients: number } | null;
  lastSnapshot: SnapshotEvent | null;
  takeSnapshot: () => void;
}

export function useStream(): StreamState {
  const [connected, setConnected] = useState(false);
  const [webcamImg, setWebcamImg] = useState<string | null>(null);
  const [webcamStats, setWebcamStats] = useState<WebcamStats | null>(null);
  const [webcamDetections, setWebcamDetections] = useState<WebcamDetection[] | null>(null);
  const [thermal, setThermal] = useState<StreamState["thermal"]>(null);
  const [serverInfo, setServerInfo] = useState<{ thermal_clients: number; browser_clients: number } | null>(null);
  const [lastSnapshot, setLastSnapshot] = useState<SnapshotEvent | null>(null);
  const wsRef = useRef<WebSocket | null>(null);

  const takeSnapshot = useCallback(() => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ type: "take_snapshot" }));
  }, []);

  useEffect(() => {
    let stopped = false;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;

    const connect = () => {
      if (stopped) return;
      const url = streamWsUrl();
      if (!url) return;
      let ws: WebSocket;
      try {
        ws = new WebSocket(url);
      } catch (e) {
        console.warn("WS connect failed", e);
        scheduleReconnect();
        return;
      }
      wsRef.current = ws;

      ws.onopen = () => setConnected(true);
      ws.onclose = () => {
        setConnected(false);
        scheduleReconnect();
      };
      ws.onerror = () => {
        try { ws.close(); } catch {}
      };
      ws.onmessage = (ev) => {
        let data: any;
        try { data = JSON.parse(ev.data); } catch { return; }
        const evt = data?.type ?? data?.event;
        switch (evt) {
          case "hello":
            setServerInfo({
              thermal_clients: data.thermal_clients ?? 0,
              browser_clients: data.browser_clients ?? 0,
            });
            break;
          case "webcam_frame":
            setWebcamImg(data.img);
            setWebcamStats(data.stats ?? null);
            setWebcamDetections(data.detections ?? null);
            break;
          case "thermal_frame":
            setThermal({
              temps: data.temps ?? [],
              ambient: data.ambient ?? 0,
              stats: data.stats ?? null,
            });
            break;
          case "stats_update":

            break;
          case "snapshot_taken":
            setLastSnapshot(data);
            break;
          default:
            break;
        }
      };
    };

    const scheduleReconnect = () => {
      if (stopped) return;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(connect, 1500);
    };

    connect();

    return () => {
      stopped = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      try { wsRef.current?.close(); } catch {}
    };
  }, []);

  return {
    connected,
    webcamImg,
    webcamStats,
    webcamDetections,
    thermal,
    serverInfo,
    lastSnapshot,
    takeSnapshot,
  };
}
