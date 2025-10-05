# Sensor Viewer

A unified CLI tool for probing and visualizing depth/color streams from supported sensors (currently Kinect v2 and optionally Kinect v1). It offers ASCII, statistics, OpenGL windowed, recording, and playback modes; works headless over SSH; and supports simple enumeration for scripting.

## Build
The binary is built automatically with the backend:

```
./backend/build.sh SensorViewer   # builds viewer
```
Output binary: `backend/build/SensorViewer`.

## Quick Start
```
# Auto-detect first available sensor (prefers v2) show nothing (text mode baseline)
SensorViewer

# ASCII depth visualization (good for SSH / headless) for 10 seconds
SensorViewer --mode ascii -t 10

# Depth stats continuously
SensorViewer -m depth-stats

# Color stats
SensorViewer -m color-stats

# OpenGL window depth stream (requires DISPLAY)
SensorViewer -m depth-window

# Force headless fallback if DISPLAY not set
SensorViewer -m depth-window --headless

# Record 30s to file
SensorViewer -m depth-stats -r capture.dat -t 30

# Playback file once as stats
SensorViewer -p capture.dat -m depth-stats

# Loop playback at 60 FPS with ascii view
SensorViewer -p capture.dat --loop --fps 60 -m ascii
```

## Modes (`--mode` / legacy `--window`)
| Mode | Description |
|------|-------------|
| `text` (default) | Opens sensor, no periodic prints (callbacks only if you attach) |
| `ascii` / `depth-ascii` | ASCII art downsampled depth grid |
| `depth-stats` | Depth frame statistics (resolution, valid %, min/max/avg) |
| `color-stats` | Color frame stats + first pixels sample |
| `depth-window` | OpenGL grayscale normalized depth window |
| `color-window` | OpenGL color window |

`-w/--window` remains as a deprecated alias; it maps values to the new canonical names.

## Listing Sensors
```
SensorViewer --list           # human readable
SensorViewer --list-json      # machine friendly JSON
```
Example JSON:
```
[
  {"index":0,"type":"KINECT_V2","id":"v2-default"},
  {"index":1,"type":"KINECT_V1","id":"v1-index-0"}
]
```
If multiple sensors are detected in AUTO mode and stdin is a TTY, an interactive selection prompt appears.

## Auto-Detection
Selection order:
1. Enumerate v2 (simple open probe)
2. Enumerate v1 (libfreenect enumeration). Each enumerated device reported as `v1-index-N`.
3. If multiple devices and interactive terminal -> prompt. Otherwise prefer v2 if present.

Environment override:
`CALDERA_SENSOR_TYPE=KINECT_V1|KINECT_V2|V1|V2|K1|K2` (applied internally by SensorViewerCore before open).

## Headless / SSH behavior
- If `DISPLAY` is unset and a windowed mode is requested, the tool automatically downgrades to the corresponding `*-stats` mode (depth-window -> depth-stats, color-window -> color-stats) and logs a notice.
- `--headless` forces this downgrade explicitly.
- ASCII mode is recommended for compact remote visualization.

## Recording & Playback
Recording uses `SensorRecorder` (depth+color paired frames) to a binary file you can later feed with `--playback`.

Flags:
- `-r / --record <file>` start recording immediately after sensor open.
- `-p / --playback <file>` run in playback mode (no live device opened).
- `--loop` loop playback endlessly.
- `--fps <N>` set artificial playback FPS (0 or omission = default 30 for recorded file; real timing metadata replay can be a future enhancement).

You can both record and visualize (stats / ascii / window) simultaneously; recording stops automatically on exit or with an explicit stop.

## Exit & Signals
Ctrl+C triggers a graceful shutdown: callback thread stops, then window (if any) is closed and loggers shutdown.

## Extending (Future Sensors)
Add a new HAL device implementing `ISensorDevice` and extend:
- `SensorType` enum in `SensorViewerCore.h`.
- `enumerateSensors()` to probe the new device type.
- Switch in `SensorViewerCore` constructor to build the appropriate device.

## Error Handling & Fallback
- On Kinect V2 open failure, automatic fallback attempts Kinect V1 (if compiled) when sensor type was AUTO.
- If both fail, tool exits non-zero.
- Window creation failure (GLFW) reverts to stats mode.

## JSON Stability
`--list-json` output format is stable: array of objects with keys: `index`, `type`, `id`.
Future additions will add new keys but not remove existing ones.

## Return Codes
| Code | Meaning |
|------|---------|
| 0 | Success / normal exit |
| 1 | CLI usage error / sensor open failure |

## Troubleshooting
| Symptom | Suggestion |
|---------|------------|
| No sensors detected | Run `./sensor_setup.sh test`; check USB / permissions. |
| Fallback message printed | V2 open failed; confirm firmware/driver installation. |
| Window not appearing | Missing DISPLAY or remote SSH; use `--headless` or set up X forwarding. |
| Recording file empty | Very short run; ensure at least one frame callback fired (check stats mode). |
| Segfault on exit (older builds) | Fixed by removing double-owned Kinect v2 packet pipeline; update to latest. |
| `LIBUSB_ERROR_IO` spam | Use high‑quality USB3 cable & port; try different controller; disable color: `CALDERA_KINECT_V2_DISABLE_COLOR=1`. |
| High packet loss (`DepthPacketStreamParser`) | Reduce other USB traffic, shorter cable, or add active hub. Occasional small loss is tolerated. |
| Forced CPU pipeline still logs VAAPI | libfreenect2 may still initialize VAAPI internally; performance generally OK—this is informational. |
| Window stays black initially | Viewer now forces window visible before first frame; if still black, depth frames not arriving (see USB tips). |

### Environment Variables
| Variable | Effect |
|----------|-------|
| `CALDERA_SENSOR_TYPE` | Force sensor type (KINECT_V1 / KINECT_V2 / aliases V1/V2/K1/K2). |
| `CALDERA_KINECT_V2_PIPELINE=cpu` | Request CPU packet pipeline (falls back if unsupported). |
| `CALDERA_KINECT_V2_DISABLE_COLOR=1` | Skip color stream (reduces USB load & skips VA decode). |
| `CALDERA_LOG_LEVEL=debug` | Increase log verbosity (async logger). |
| `CALDERA_SKIP_LOGGER_SHUTDOWN=1` | Debug shutdown ordering issues (normally unnecessary). |
| `DISPLAY` | If unset and window mode chosen → auto fallback to stats. |
| `LIBFREENECT2_DISABLE_RGB=1` | (Upstream libfreenect2) Disables RGB processing internally. |

### Known Limitations
- VAAPI pipeline messages may appear even when CPU pipeline requested; this is benign.
- Kinect v2 frame loss on some Linux USB controllers is a known upstream issue—mitigated by better cable/controller.
- Current window loop uses simple ~60 FPS sleep; no adaptive vsync timing yet.


## Roadmap / TODO
- Proper libfreenect2 multi-device enumeration (serials).
- Unified percentiles / latency overlay in windowed mode.
- Multi-sensor simultaneous view grid.
- Colorized ASCII depth (gradient) option.

---
Generated automatically as part of refactor (Phase: Viewer unification).
