# Face Tracking

Raspberry Pi 5 face-detection pipeline with modular C++ camera and detector processes, a Python/React Web HMI, and a replaceable middleware seam. The default deployment uses Zenoh 1.10; ROS 2 is not a build or runtime dependency.

## Modules

- `modules/face_tracking_schemas`: middleware-neutral DTOs, validation, Protobuf v2 schema and codecs.
- `modules/camera_service`: UVC capture, latest-frame buffering, JPEG encoding and rate limiting.
- `modules/face_detector_tracker`: OpenCV DNN inference, latest-only scheduling, YOLO post-processing and stable short-term face tracks.
- `modules/pixel_center_controller`: middleware-neutral target validation and bounded pixel-centering P control.
- `modules/servo_driver`: middleware-neutral command validation, commanded-angle state machine, soft limits, and the Raspberry Pi lgpio PWM backend.
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

The HMI draws every currently observed face on the live video and lists the same tracks in the side panel. Select a face by clicking its box or list row; use **Cancel tracking** to return to `NO_TARGET`. The first four consecutive frames without the selected `track_id` enter `MISSING` and hold the current angles. The fifth enters `LOST` and returns to Home; the same track can be reacquired for one second. An empty list is normal when nobody is in view. The displayed Pan/Tilt angles are software-commanded values because the servos have no position feedback.

`run.sh` does not enable Zenoh on boot. Bringup reuses `zenohd.service`, or interrupts a manual `/usr/bin/zenohd` only when it is owned by the current user. It never stops an unrelated listener and leaves the system router active after application exit.

## Wire interfaces

Keys remain under `face_tracking/{device_id}`:

- `camera/image`: JPEG payload with Protobuf v2 `FrameMetadata` attachment; drop-old congestion policy.
- `camera/status`: Protobuf v2 `CameraStatus`.
- `detections`: Protobuf v2 `DetectionResult`.
- `diagnostics/detector`: Protobuf v2 `DetectorStatus`.
- `target/selected`: Protobuf v2 `SelectedTargetObservation` from the HMI target manager.
- `pan_tilt/delta_cmd`: Protobuf v2 `PanTiltDelta` from the pixel-center controller.
- `diagnostics/pixel_center_controller`: Protobuf v2 `PixelCenterControllerStatus`.
- `pan_tilt/commanded_state`: Protobuf v2 `PanTiltCommandedState`, also available through a Zenoh queryable.
- `liveliness/camera`, `liveliness/detector`, `liveliness/pixel_center_controller`, and `liveliness/servo_driver`: process liveliness tokens.

Every detection includes a tracker-process identity plus a `track_id`; the pair is the stable selection identity. The controller runs at 20 Hz and accepts only fresh `TRACKING` observations. It applies a 30 px horizontal and 24 px vertical deadband, 0.01 degree/px proportional gain, and per-frame limits of 1.5 degrees pan and 1.0 degree tilt. MISSING, deadband, duplicate, and out-of-order decisions hold the current commanded angles; LOST, NO_TARGET, stale observations, controller liveliness loss, or 1.5 seconds without valid controller activity return to Home.

The servo driver uses GPIO17 for the 270-degree Pan servo and GPIO27 for the 180-degree Tilt servo. Home is Pan 135 degrees / Tilt 20 degrees. Pan is limited to its rated 0-270 degree range; Tilt has a 15-45 degree tracking soft limit. A candidate that crosses a limit leaves that axis unchanged instead of saturating it, while the other axis may still move. Both axes default to a nominal 500-2500 microsecond pulse mapping at 50 Hz. Use an independent servo power supply with a common ground, and verify each axis direction with a small unloaded movement before sustained tracking.

With the normal system stopped, the standalone hardware sweep can continuously exercise both servos inside those configured limits. It starts at Home, advances by 1 degree every 50 ms, reverses independently at each endpoint, and releases PWM on Ctrl+C:

```bash
FACE_TRACKING_BUILD_DIR=build-opencv412 \
  build-opencv412/bin/face_tracking_servo_sweep_test config/default.yaml 1 50
```

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

- Release build: 32/32 CTest cases, 20/20 HMI pytest cases, Ruff, frontend typecheck/build, and `npm audit` all pass.
- Camera: 1280x720 capture at 29.9 FPS and JPEG publication at 10.0 FPS, with latest-only replacement and no backlog.
- Cross-language transport: camera, detector, target, controller, status, attachment, and liveliness channels interoperate across C++ and Python.
- Tracking/control transport: synthetic target selection verifies C++ controller output (`pan +1.0`, `tilt -0.6`) and automatic zero output after the configured freshness limit. The real empty-camera scene remains stable at zero faces, `NO_TARGET`, and safe zero control output.
- Live-face latency under the thermally limited full-system load measured 380–551 ms from capture to controller decision, so the deployment profile uses a 600 ms freshness limit. Adequate active cooling is still required to restore the 5 FPS detector performance gate and provide latency margin.
- Process lifecycle: bringup starts camera, detector, controller, servo driver, and HMI together; SIGINT/SIGTERM completes within the bounded shutdown window even with live browser WebSockets.
- Fixed Lena image: C++ OpenCV DNN produces one box `(212, 187, 144, 202)` at confidence `0.833418`; the former Python/Ultralytics implementation produces `[212.097, 186.852, 356.496, 389.138]` at `0.833417`.
- Detector benchmark while the CPU is not thermally limited: C++ averages 154-160 ms (6.3-6.5 FPS), versus 442 ms (2.26 FPS) for the former Python implementation. During the same comparison the C++ process used about 354-369% CPU and 126 MiB RSS; Python/Ultralytics used about 107-134% CPU and 846 MiB peak RSS. C++ trades more parallel CPU utilization for 2.8x throughput while reducing memory substantially.

The tested Pi has no registered cooling device. Under a sustained camera + detector + HMI load it reaches the firmware soft-temperature limit (about 80 C; `get_throttled` bit 3), and end-to-end detector throughput falls to 3.4-4.3 FPS. The 5 FPS sustained hardware gate therefore requires adequate Pi 5 active cooling and must be rerun after cooling is installed. The normal integration run verifies transport compatibility; `FACE_TRACKING_PI_PERFORMANCE=1` separately enables the real-time gate so the hardware limitation remains explicit.
