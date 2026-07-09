# i.MX6ULL Camera Vision Gateway

V4L2 camera capture, HTTP MJPEG streaming, LCD preview, and lightweight motion
detection for the i.MX6ULL platform.

This project is focused on a practical embedded Linux camera gateway:

```text
USB UVC camera
  -> V4L2 MJPG capture
  -> HTTP MJPEG /stream
  -> /snapshot and /metrics
  -> JPEG decode
  -> LCD framebuffer preview
  -> motion mask and event snapshots
```

## Features

- V4L2 mmap camera capture
- Camera format, resolution, and fps enumeration
- MJPG capture as the recommended production path
- HTTP MJPEG streaming with multiple client threads
- `/snapshot` endpoint for the latest JPEG frame
- `/metrics` endpoint for runtime status
- LCD preview through Linux framebuffer `/dev/fb0`
- JPEG decode to RGB565 when `USE_LIBJPEG=1`
- Lightweight frame-difference motion detection
- Red motion mask overlay on the LCD preview
- Motion event snapshots
- Runtime dependency inspection for dynamic libjpeg builds

## Hardware Baseline

Tested development baseline:

```text
Board: 100ask i.MX6ULL
Camera: USB UVC camera
Camera node: /dev/video1
LCD framebuffer: /dev/fb0
Recommended format: MJPG
Recommended resolution: 640x480
Recommended fps request: 15
```

Known device notes:

```text
/dev/video1 -> USB UVC camera
/dev/video0 -> pxp device, not the normal capture camera
```

YUYV is unstable on the current board and camera combination. Keep MJPG as the
main path. YUYV/NV12/RGBP options are retained for experiments and debugging,
not as the recommended gateway mode.

## Directory Layout

```text
standardized_camera_app/
  include/       Public project headers
  src/           Application source code
  tests/         Host-side tests
  scripts/       Build and runtime helper scripts
  docs/          Project plan, progress, and learning notes
  Makefile
  README.md
```

## Build

Basic static build without JPEG decode support:

```bash
cd ~/Videos/cam-1-capture10frames-main/standardized_camera_app
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
```

Build with JPEG decode support for MJPG LCD preview and motion detection:

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 STATIC=0
```

Output:

```text
build/ov5640_capture
```

Deploy:

```bash
adb push build/ov5640_capture /root/
adb shell
cd /root
chmod +x ov5640_capture
```

If the board reports a missing `libjpeg.so.*`, inspect runtime dependencies:

```bash
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 STATIC=0 runtime-libs
```

Then copy the ARM libjpeg runtime library from the cross sysroot to the board.

## Quick Start

List camera formats:

```bash
./ov5640_capture --list-formats -d /dev/video1
```

Capture metrics only:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 100 --no-save
```

Save individual MJPG frames:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 10 --save-frames /tmp/frames
```

Run HTTP MJPEG streaming:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

Run HTTP streaming and LCD preview:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0
```

Run the full gateway mode with motion detection:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0 \
  --motion-detect --motion-threshold 15 --motion-dir /tmp/motion
```

On the Ubuntu host:

```bash
adb forward tcp:8080 tcp:8080
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

Open the stream in a browser:

```text
http://127.0.0.1:8080/stream
```

## HTTP API

The built-in HTTP server is intentionally small and does not require Nginx,
FFmpeg, RTSP, or another external server.

Endpoints:

```text
GET /stream    MJPEG stream
GET /snapshot  Latest JPEG frame
GET /metrics   Runtime status
```

Important metrics:

```text
connected_clients=N
captured_frames=N
latest_frame_size=N
lcd_preview=enabled
lcd_frames=N
motion_detect=enabled
motion_detected=0 or 1
motion_active_blocks=N
motion_box_peak_delta=N
motion_events=N
motion_snapshots=N
motion_errors=0
```

`motion_detected` is the current frame state. If motion happened and then
stopped, it may return to `0`. Use `motion_events`, `motion_snapshots`,
`motion_active_blocks`, and `motion_box_peak_delta` to observe historical and
current motion activity.

## Motion Threshold

Motion detection uses lightweight frame difference, not AI inference.

Recommended threshold values:

```text
8   Very sensitive, useful for functional tests
15  Recommended starting point
20  More stable in normal scenes
30  Conservative, detects only obvious motion
```

If the LCD marks red areas while the scene is still, increase
`--motion-threshold`. If obvious movement is missed, decrease it.

## LCD Preview

The LCD framebuffer cannot display compressed MJPG bytes directly. The display
path is:

```text
MJPG frame -> JPEG decode -> RGB565 -> /dev/fb0
```

The HTTP stream still sends original MJPG frames directly to browser clients, so
it does not pay the cost of JPEG re-encoding.

Framebuffer color-bar test:

```bash
./ov5640_capture --fb-test /dev/fb0
```

## Options

| Option | Description | Default |
| --- | --- | --- |
| `-d DEV` | V4L2 device node | `/dev/video1` |
| `-w W` | Width | `1024` |
| `-h H` | Height | `600` |
| `-f FMT` | Pixel format: `MJPG`, `MJPEG`, `YUYV`, `NV12`, `RGBP` | `YUYV` |
| `-r FPS` | Requested frame rate | `30` |
| `-n N` | Frame count. `0` means continuous capture | `10` |
| `-o FILE` | Continuous output file | `output.yuv` |
| `--no-save` | Capture and print metrics without writing output | off |
| `--save-frames DIR` | Save each captured frame into DIR | off |
| `--http-mjpeg PORT` | Enable HTTP MJPEG server | off |
| `--fb-preview DEV` | Preview camera frames on framebuffer DEV | off |
| `--fb-test DEV` | Draw framebuffer test pattern | off |
| `--motion-detect` | Enable frame-difference motion detection | off |
| `--motion-threshold N` | Motion threshold | `20` |
| `--motion-dir DIR` | Save one MJPG snapshot for each motion event | `/tmp/motion` |
| `-L`, `--list-formats` | List supported formats, frame sizes, and fps | off |

## Tests

The frame queue module is retained as a learning and host-side test module for
the producer-consumer model. It is not part of the main gateway binary.

```bash
make test-frame-queue
```

Expected output:

```text
frame_queue_test passed
```

## Known Limits

- Main validated path is `640x480 MJPG`.
- YUYV capture is unstable on the current hardware combination.
- Motion detection is traditional frame difference, not AI target detection.
- i.MX6ULL has no NPU and is not suitable for real-time YOLO-style inference.
- RTSP/H.264 is reserved for a later platform or encoder-supported extension.

## Documentation

- `docs/full_project_plan.md`: full project roadmap
- `docs/progress.md`: experiment and implementation history
- `docs/learning_roadmap.md`: learning path
- `docs/git_workflow.md`: Git workflow notes
