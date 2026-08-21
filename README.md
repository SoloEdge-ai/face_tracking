# Face Tracking Camera HMI

This first version streams USB-camera JPEG frames at 10 Hz over Zenoh and serves a LAN-only HMI at port 8080.

## Install and run

On the Raspberry Pi:

```bash
cd /home/friden/Code/face_tracking
chmod +x setup.sh run.sh
./setup.sh
./run.sh
```

Open `http://192.168.50.2:8080` from the local network. The HMI has no authentication by design.

`run.sh` does not enable Zenoh on boot. It only verifies/reuses `zenohd.service`, or safely replaces a manual `/usr/bin/zenohd` owned by the current user. It never stops another process, uses `SIGINT` only, and leaves the system service running when the script exits.

## Interfaces

* `face_tracking/{device_id}/camera/image`: JPEG payload with UTF-8 JSON metadata attachment (schema 1).
* `face_tracking/{device_id}/camera/status`: JSON status once per second.
* `face_tracking/{device_id}/liveliness/camera`: camera-driver liveliness token.

The default device is the stable UVC symlink in `config/default.toml`. Set `FACE_TRACKING_DEVICE_ID` to override the key expression device identifier, or `FACE_TRACKING_CONFIG` to use another TOML file.

## Development

```bash
. .venv/bin/activate
pytest
cd frontend && npm run typecheck && npm run build
```
