#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"

sudo apt-get update
sudo apt-get install -y \
  ca-certificates cmake curl g++ git libavcodec-dev libavformat-dev libavutil-dev libgtest-dev libopencv-dev libprotobuf-dev \
  libswscale-dev libyaml-cpp-dev libzenohc-dev=1.10.0 nodejs npm protobuf-compiler \
  python3-eclipse-zenoh=1.10.0 python3-protobuf python3-venv

sha256sum --check models/yolov8n-face-lindevs.onnx.sha256
protoc --proto_path modules/face_tracking_schemas/proto \
  --python_out modules/web_hmi_target_manager/service/src/face_tracking_hmi/generated \
  modules/face_tracking_schemas/proto/face_tracking_v2.proto

python3 -m venv --clear --system-site-packages .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e 'modules/web_hmi_target_manager/service[dev]'
.venv/bin/python -m pip freeze --local --exclude-editable > modules/web_hmi_target_manager/service/requirements.lock

cmake --fresh -S . -B build -DCMAKE_BUILD_TYPE=Release -DFACE_TRACKING_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
.venv/bin/python -s -m ruff check --config modules/web_hmi_target_manager/service/pyproject.toml \
  modules/web_hmi_target_manager/service/src modules/web_hmi_target_manager/service/tests tests/integration/test_zenoh_v2.py
.venv/bin/python -s -m pytest modules/web_hmi_target_manager/service/tests

pushd modules/web_hmi_target_manager/web >/dev/null
npm ci
npm run typecheck
npm run build
popd >/dev/null

echo "Setup complete. Start with: ./run.sh"
