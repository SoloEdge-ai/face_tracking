import { useCallback, useEffect, useRef, useState } from "react";

type CameraStatus = {
  state: "STARTING" | "STREAMING" | "STALE" | "OFFLINE";
  width?: number; height?: number; hmi_fps?: number; frame_age_ms?: number; invalid_frames?: number;
  driver?: { last_error?: string | null; publish_fps?: number };
};
type Box = { x: number; y: number; width: number; height: number; confidence: number };
type Detection = { image_width: number; image_height: number; boxes: Box[] };
const initial: CameraStatus = { state: "STARTING" };
const number = (value: number | undefined, suffix = "") => value === undefined ? "—" : `${value}${suffix}`;
const wsUrl = (path: string) => `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}${path}`;

export function App() {
  const [status, setStatus] = useState<CameraStatus>(initial);
  const [detection, setDetection] = useState<Detection | null>(null);
  const canvas = useRef<HTMLCanvasElement>(null);

  const receiveStatus = useCallback((value: CameraStatus) => setStatus(value), []);
  const receiveDetection = useCallback((value: Detection) => setDetection(value.boxes.length ? value : null), []);
  useSocket("/ws/status", receiveStatus);
  useSocket("/ws/detections", receiveDetection);
  useEffect(() => {
    if (!detection) return;
    const timer = window.setTimeout(() => setDetection(null), 1000);
    return () => window.clearTimeout(timer);
  }, [detection]);
  useEffect(() => {
    const element = canvas.current;
    if (!element) return;
    const draw = () => {
      const context = element.getContext("2d");
      if (!context) return;
      const { width, height } = element.getBoundingClientRect();
      const ratio = devicePixelRatio;
      element.width = width * ratio; element.height = height * ratio;
      context.scale(ratio, ratio); context.clearRect(0, 0, width, height);
      if (!detection) return;
      const scale = Math.min(width / detection.image_width, height / detection.image_height);
      const offsetX = (width - detection.image_width * scale) / 2;
      const offsetY = (height - detection.image_height * scale) / 2;
      context.strokeStyle = "#53dc9a"; context.fillStyle = "#53dc9a"; context.lineWidth = 2; context.font = "600 14px system-ui";
      detection.boxes.forEach(box => {
        const x = offsetX + box.x * scale, y = offsetY + box.y * scale;
        context.strokeRect(x, y, box.width * scale, box.height * scale);
        context.fillText(`FACE ${(box.confidence * 100).toFixed(0)}%`, x + 4, Math.max(14, y - 6));
      });
    };
    const observer = new ResizeObserver(draw); observer.observe(element); draw();
    return () => observer.disconnect();
  }, [detection]);

  return <main>
    <header><div><p className="eyebrow">FACE TRACKING · RASPBERRY PI</p><h1>Camera HMI</h1></div><span className={`badge ${status.state.toLowerCase()}`}>{status.state}</span></header>
    <section className="video-card"><img src="/api/camera/stream.mjpg" alt="Live camera stream" /><canvas ref={canvas} aria-label="Face detection overlay" />{status.state !== "STREAMING" && <div className="video-message">等待实时画面…</div>}</section>
    <section className="metrics"><Metric label="分辨率" value={status.width && status.height ? `${status.width} × ${status.height}` : "—"} /><Metric label="实际 FPS" value={number(status.hmi_fps, " fps")} /><Metric label="帧龄" value={number(status.frame_age_ms, " ms")} /><Metric label="人脸" value={detection ? String(detection.boxes.length) : "0"} /></section>
    <section className="detail"><span>无效帧：{status.invalid_frames ?? 0}</span><span>错误：{status.driver?.last_error || "无"}</span></section>
  </main>;
}

function useSocket<T>(path: string, receive: (value: T) => void) {
  useEffect(() => { let socket: WebSocket | undefined, disposed = false; const connect = () => { socket = new WebSocket(wsUrl(path)); socket.onmessage = event => { try { receive(JSON.parse(event.data) as T); } catch { /* Ignore malformed server data. */ } }; socket.onclose = () => { if (!disposed) window.setTimeout(connect, 1500); }; }; connect(); return () => { disposed = true; socket?.close(); }; }, [path, receive]);
}

function Metric({ label, value }: { label: string; value: string }) { return <article><p>{label}</p><strong>{value}</strong></article>; }
