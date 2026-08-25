import { PointerEvent, useCallback, useEffect, useMemo, useRef, useState } from "react";

type CameraStatus = {
  state: "STARTING" | "STREAMING" | "STALE" | "OFFLINE";
};
type TrackingState = "NO_TARGET" | "TRACKING" | "MISSING" | "LOST";
type Box = { x: number; y: number; width: number; height: number; confidence: number; track_id: number };
type Detection = {
  source_instance_id?: string; tracker_instance_id?: string; sequence?: number;
  image_width?: number; image_height?: number; boxes: Box[];
  selected_track_id?: number | null; tracking_state?: TrackingState;
};
type ServoState = {
  commanded_pan_deg?: number; commanded_tilt_deg?: number;
};
const initial: CameraStatus = { state: "STARTING" };
const number = (value: number | undefined, suffix = "") => value === undefined ? "—" : `${value.toFixed(1)}${suffix}`;
const wsUrl = (path: string) => `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}${path}`;

export function App() {
  const [status, setStatus] = useState<CameraStatus>(initial);
  const [detection, setDetection] = useState<Detection>({ boxes: [], tracking_state: "NO_TARGET" });
  const [servo, setServo] = useState<ServoState>({});
  const [selectionError, setSelectionError] = useState<string | null>(null);
  const canvas = useRef<HTMLCanvasElement>(null);

  useSocket("/ws/status", useCallback((value: CameraStatus) => setStatus(value), []));
  useSocket("/ws/detections", useCallback((value: Detection) => setDetection(value), []));
  useSocket("/ws/servo", useCallback((value: ServoState) => setServo(value), []));

  const faces = useMemo(
    () => [...detection.boxes].filter(face => face.track_id > 0).sort((a, b) => a.track_id - b.track_id),
    [detection.boxes],
  );
  const selected = detection.selected_track_id ?? null;

  const selectFace = useCallback(async (face: Box) => {
    if (!detection.source_instance_id || !detection.tracker_instance_id || detection.sequence === undefined) return;
    const response = await fetch("/api/target", {
      method: "PUT", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source_instance_id: detection.source_instance_id, tracker_instance_id: detection.tracker_instance_id, sequence: detection.sequence, track_id: face.track_id }),
    });
    if (!response.ok) { setSelectionError("目标已经过期，请重新选择"); return; }
    setSelectionError(null);
    const target = await response.json() as { selected_track_id?: number; tracking_state?: TrackingState };
    setDetection(current => ({ ...current, ...target }));
  }, [detection.source_instance_id, detection.tracker_instance_id, detection.sequence]);

  const clearTarget = useCallback(async () => {
    const response = await fetch("/api/target", { method: "DELETE" });
    if (!response.ok) return;
    const target = await response.json() as { selected_track_id?: number | null; tracking_state?: TrackingState };
    setDetection(current => ({ ...current, ...target }));
  }, []);

  const onCanvasPointer = useCallback((event: PointerEvent<HTMLCanvasElement>) => {
    if (!detection.image_width || !detection.image_height) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const scale = Math.min(rect.width / detection.image_width, rect.height / detection.image_height);
    const offsetX = (rect.width - detection.image_width * scale) / 2;
    const offsetY = (rect.height - detection.image_height * scale) / 2;
    const x = (event.clientX - rect.left - offsetX) / scale;
    const y = (event.clientY - rect.top - offsetY) / scale;
    const hit = [...faces].sort((a, b) => a.width * a.height - b.width * b.height)
      .find(face => x >= face.x && x <= face.x + face.width && y >= face.y && y <= face.y + face.height);
    if (hit) void selectFace(hit);
  }, [detection.image_width, detection.image_height, faces, selectFace]);

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
      if (!detection.image_width || !detection.image_height) return;
      const scale = Math.min(width / detection.image_width, height / detection.image_height);
      const offsetX = (width - detection.image_width * scale) / 2;
      const offsetY = (height - detection.image_height * scale) / 2;
      context.lineWidth = 3; context.font = "700 14px system-ui";
      detection.boxes.forEach(face => {
        const isSelected = face.track_id === selected;
        context.strokeStyle = isSelected ? "#ffcc66" : "#53dc9a";
        context.fillStyle = context.strokeStyle;
        const x = offsetX + face.x * scale, y = offsetY + face.y * scale;
        context.strokeRect(x, y, face.width * scale, face.height * scale);
        context.fillText(`#${face.track_id} ${(face.confidence * 100).toFixed(0)}%`, x + 4, Math.max(16, y - 7));
      });
    };
    const observer = new ResizeObserver(draw); observer.observe(element); draw();
    return () => observer.disconnect();
  }, [detection, selected]);

  return <main>
    <header><div><p className="eyebrow">FACE TRACKING · RASPBERRY PI</p><h1>Target Tracking HMI</h1></div><span className={`badge ${status.state.toLowerCase()}`}>{status.state}</span></header>
    <section className="workspace">
      <div className="video-card"><img src="/api/camera/stream.mjpg" alt="Live camera stream" /><canvas ref={canvas} onPointerDown={onCanvasPointer} aria-label="可点击的人脸追踪画面" />{status.state !== "STREAMING" && <div className="video-message">等待实时画面…</div>}</div>
      <aside className="faces-panel">
        <div className="panel-title"><div><p className="eyebrow">VISIBLE FACES</p><h2>检测到的人脸</h2></div><strong>{faces.length}</strong></div>
        {faces.length === 0 ? <p className="empty">当前未检测到人脸。现场无人时这是正常现象。</p> : <div className="face-list">{faces.map(face => <button key={face.track_id} className={face.track_id === selected ? "face selected" : "face"} onClick={() => void selectFace(face)}><span>人脸 #{face.track_id}</span><small>置信度 {(face.confidence * 100).toFixed(0)}%</small></button>)}</div>}
        <div className={`target-state ${(detection.tracking_state || "NO_TARGET").toLowerCase()}`}><span>{detection.tracking_state || "NO_TARGET"}</span><strong>{selected ? `目标 #${selected}` : "请选择目标"}</strong></div>
        {selected && <button className="clear" onClick={() => void clearTarget()}>取消追踪</button>}
        {selectionError && <p className="error">{selectionError}</p>}
      </aside>
    </section>
    <section className="detail"><span>人脸 {faces.length}</span><span>Pan {number(servo.commanded_pan_deg, "°")}</span><span>Tilt {number(servo.commanded_tilt_deg, "°")}</span><span className="muted">软件指令角</span></section>
  </main>;
}

function useSocket<T>(path: string, receive: (value: T) => void) {
  useEffect(() => { let socket: WebSocket | undefined, disposed = false; const connect = () => { socket = new WebSocket(wsUrl(path)); socket.onmessage = event => { try { receive(JSON.parse(event.data) as T); } catch { /* Ignore malformed server data. */ } }; socket.onclose = () => { if (!disposed) window.setTimeout(connect, 1500); }; }; connect(); return () => { disposed = true; socket?.close(); }; }, [path, receive]);
}
