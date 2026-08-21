import { useEffect, useState } from "react";

type CameraStatus = {
  state: "STARTING" | "STREAMING" | "STALE" | "OFFLINE";
  width?: number;
  height?: number;
  hmi_fps?: number;
  frame_age_ms?: number;
  invalid_frames?: number;
  driver?: { last_error?: string | null; publish_fps?: number };
};

const initial: CameraStatus = { state: "STARTING" };
const number = (value: number | undefined, suffix = "") => value === undefined ? "—" : `${value}${suffix}`;

export function App() {
  const [status, setStatus] = useState<CameraStatus>(initial);

  useEffect(() => {
    let socket: WebSocket | undefined;
    let disposed = false;
    const connect = () => {
      socket = new WebSocket(`${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws/status`);
      socket.onmessage = event => {
        try { setStatus(JSON.parse(event.data) as CameraStatus); } catch { /* Ignore malformed status. */ }
      };
      socket.onclose = () => { if (!disposed) window.setTimeout(connect, 1500); };
    };
    connect();
    return () => { disposed = true; socket?.close(); };
  }, []);

  return <main>
    <header><div><p className="eyebrow">FACE TRACKING · RASPBERRY PI</p><h1>Camera HMI</h1></div><span className={`badge ${status.state.toLowerCase()}`}>{status.state}</span></header>
    <section className="video-card">
      <img src="/api/camera/stream.mjpg" alt="Live camera stream" />
      <canvas aria-label="Detection overlay placeholder" />
      {status.state !== "STREAMING" && <div className="video-message">等待实时画面…</div>}
    </section>
    <section className="metrics">
      <Metric label="分辨率" value={status.width && status.height ? `${status.width} × ${status.height}` : "—"} />
      <Metric label="实际 FPS" value={number(status.hmi_fps, " fps")} />
      <Metric label="帧龄" value={number(status.frame_age_ms, " ms")} />
      <Metric label="发布 FPS" value={number(status.driver?.publish_fps, " fps")} />
    </section>
    <section className="detail"><span>无效帧：{status.invalid_frames ?? 0}</span><span>错误：{status.driver?.last_error || "无"}</span></section>
  </main>;
}

function Metric({ label, value }: { label: string; value: string }) {
  return <article><p>{label}</p><strong>{value}</strong></article>;
}
