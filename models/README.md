# Detector model

`yolov8n-face-lindevs.onnx` is the production detector artifact. It was exported from the
project's baseline `yolov8n-face-lindevs.pt` with Ultralytics 8.4.117, fixed input
`1x3x640x640`, ONNX opset 11, and graph simplification (ONNX 1.22.0 / onnxslim 0.1.96).

The `.pt` source and the export toolchain are validation-only assets; Torch and Ultralytics
must not be installed by `setup.sh` or imported by a production process. Verify the committed
artifact with:

```bash
yolo export model=models/yolov8n-face-lindevs.pt format=onnx imgsz=640 opset=11 simplify=True dynamic=False
sha256sum --check models/yolov8n-face-lindevs.onnx.sha256
```

Expected output shape: `1x5x8400` (`cx`, `cy`, `width`, `height`, face confidence).
