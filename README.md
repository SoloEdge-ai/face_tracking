# Face Tracking

Raspberry Pi 5 face-detection pipeline with modular C++ camera and detector processes, a Python/React Web HMI, and a replaceable middleware seam. The default deployment uses Zenoh 1.10; ROS 2 is not a build or runtime dependency.

## Modules

- `modules/face_tracking_schemas`: middleware-neutral DTOs, validation, Protobuf v2 schema and codecs.
- `modules/camera_service`: UVC capture, latest-frame buffering, JPEG encoding and rate limiting.
- `modules/face_detector_tracker`: OpenCV DNN inference, latest-only scheduling and YOLO post-processing.
- `adapters/zenoh`: Zenoh publishers/subscriber and the C++ process entrypoints.
- `modules/web_hmi_target_manager`: FastAPI HMI service, transport adapter, and React application.
- `modules/pan_tilt_bringup`: safe `zenohd` handover and child-process lifecycle.

Business modules do not include Zenoh, Protobuf, or ROS types in their interfaces. A future ROS 2 adapter can translate ROS messages to the same DTOs without changing camera, detector, or HMI state logic.

## Install and run

```bash
cd /home/friden/Code/face_tracking
chmod +x setup.sh run.sh
./setup.sh
./run.sh
```

Open `http://192.168.50.2:8080`. Set `FACE_TRACKING_CONFIG` to select another YAML profile or `FACE_TRACKING_DEVICE_ID` to override the configured device identifier.

`run.sh` does not enable Zenoh on boot. Bringup reuses `zenohd.service`, or interrupts a manual `/usr/bin/zenohd` only when it is owned by the current user. It never stops an unrelated listener and leaves the system router active after application exit.

## Wire interfaces

Keys remain under `face_tracking/{device_id}`:

- `camera/image`: JPEG payload with Protobuf v2 `FrameMetadata` attachment; drop-old congestion policy.
- `camera/status`: Protobuf v2 `CameraStatus`.
- `detections`: Protobuf v2 `DetectionResult`.
- `diagnostics/detector`: Protobuf v2 `DetectorStatus`.
- `liveliness/camera` and `liveliness/detector`: process liveliness tokens.

## Development

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
.venv/bin/python -m pytest modules/web_hmi_target_manager/service/tests
cd modules/web_hmi_target_manager/web && npm run typecheck && npm run build
```

With `./run.sh` active, execute the cross-language Zenoh smoke test in another shell:

```bash
FACE_TRACKING_E2E=1 .venv/bin/python -m pytest tests/integration/test_zenoh_v2.py
```

On the cooled production Pi, add `FACE_TRACKING_PI_PERFORMANCE=1` to enforce the real-time detector gate in the same test.

Measure the pinned detector artifact on a representative image with:

```bash
build/bin/face_tracking_detector_benchmark \
  models/yolov8n-face-lindevs.onnx /path/to/image.jpg
```

The committed ONNX artifact is the only detector runtime model. Torch and Ultralytics are not production dependencies.
The default build pins OpenCV 4.12 because Debian 12's OpenCV 4.6 cannot execute the YOLOv8 detection head. Set `FACE_TRACKING_USE_SYSTEM_OPENCV=ON` only when the system provides OpenCV 4.9 or newer.

## Raspberry Pi 5 validation (2026-08-23)

- Release build: 14/14 CTest cases, 12/12 HMI pytest cases, Ruff, frontend typecheck/build, and `npm audit` all pass.
- Camera: 1280x720 capture at 29.9 FPS and JPEG publication at 10.0 FPS, with latest-only replacement and no backlog.
- Cross-language transport: all four Zenoh v2 channels decode in Python, including the JPEG metadata attachment and both diagnostics payloads.
- Fixed Lena image: C++ OpenCV DNN produces one box `(212, 187, 144, 202)` at confidence `0.833418`; the former Python/Ultralytics implementation produces `[212.097, 186.852, 356.496, 389.138]` at `0.833417`.
- Detector benchmark while the CPU is not thermally limited: C++ averages 154-160 ms (6.3-6.5 FPS), versus 442 ms (2.26 FPS) for the former Python implementation.

The tested Pi has no registered cooling device. Under a sustained camera + detector + HMI load it reaches the firmware soft-temperature limit (about 80 C; `get_throttled` bit 3), and end-to-end detector throughput falls to 3.4-4.3 FPS. The 5 FPS sustained hardware gate therefore requires adequate Pi 5 active cooling and must be rerun after cooling is installed. The normal integration run verifies transport compatibility; `FACE_TRACKING_PI_PERFORMANCE=1` separately enables the real-time gate so the hardware limitation remains explicit.
