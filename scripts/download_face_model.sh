#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
model_dir="$root_dir/models"
model_path="$model_dir/yolov8n-face-lindevs.pt"
expected_sha256="b038ca653b503453a94f6e12d76feca6840b2a97d7a1322b4498c5e922f29832"
url="https://github.com/lindevs/yolov8-face/releases/download/1.0.1/yolov8n-face-lindevs.pt"

mkdir -p "$model_dir"
if [[ ! -f "$model_path" ]]; then
  curl --fail --location --retry 3 --output "$model_path" "$url"
fi
echo "$expected_sha256  $model_path" | sha256sum --check --status
echo "Face model ready: $model_path"
