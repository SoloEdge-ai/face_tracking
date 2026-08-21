#!/usr/bin/env bash
# Install the application dependencies; this never changes zenohd configuration.
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root_dir"

sudo apt-get update
sudo apt-get install -y python3-eclipse-zenoh=1.10.0 python3-venv python3-opencv

python3 -m venv --system-site-packages .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e '.[dev]'
.venv/bin/python -m pip freeze --exclude-editable > requirements.lock

pushd frontend >/dev/null
npm ci
npm run build
popd >/dev/null

echo "Setup complete. Start with: ./run.sh"
