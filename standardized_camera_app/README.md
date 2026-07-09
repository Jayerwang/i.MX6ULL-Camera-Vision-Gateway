# Standardized Camera Capture App

This is the standardized V4L2 capture app for the i.MX6ULL camera project.

Current focus:

- V4L2 mmap capture
- Device format/resolution/fps enumeration
- YUYV/NV12/RGB565/MJPG capture
- Continuous raw output
- Capture-only metrics with `--no-save`
- MJPEG per-frame saving with `--save-frames`
- Single-client HTTP MJPEG streaming with `--http-mjpeg`

## Directory Layout

```text
standardized_camera_app/
  include/
  src/
  scripts/
  docs/
  output/
  Makefile
```

## Build

```bash
cd /home/book/Videos/cam-1-capture10frames-main/standardized_camera_app
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
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

## Options

| Option | Description | Default |
| --- | --- | --- |
| `-d DEV` | V4L2 device node | `/dev/video1` |
| `-w W` | Width | `1024` |
| `-h H` | Height | `600` |
| `-f FMT` | Pixel format: `YUYV` / `NV12` / `RGBP` / `MJPG` / `MJPEG` | `YUYV` |
| `-r FPS` | Requested frame rate | `30` |
| `-n N` | Frame count. `0` means continuous capture | `10` |
| `-o FILE` | Continuous output file | `output.yuv` |
| `--no-save` | Capture and print metrics without writing output | off |
| `--save-frames DIR` | Save each captured frame into DIR. MJPG/MJPEG frames are saved as `.jpg` | off |
| `--http-mjpeg PORT` | Stream MJPEG over HTTP on PORT | off |
| `-L`, `--list-formats` | List supported formats, frame sizes and fps | off |

## List Device Formats

```bash
./ov5640_capture --list-formats -d /dev/video1
./ov5640_capture --list-formats -d /dev/video0
```

Your current useful camera is:

```text
/dev/video1
Driver: uvcvideo
Card: USB 2.0 Camera
```

`/dev/video0` is `pxp` on your board and is not a normal capture camera.

## Current Recommended Baseline

Based on your experiments, use this as the main development baseline:

```text
device: /dev/video1
format: MJPG
size:   640x480
fps:    15 or 30 requested, actual about 15-22 fps
```

YUYV is unstable on this board/camera combination. 1280x720 MJPG works, but is slow and drops buffers.

## Capture Metrics Only

Use this to measure capture fps without file writing:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 30 -n 100 --no-save
./ov5640_capture -d /dev/video1 -w 1280 -h 720 -f MJPG -r 30 -n 100 --no-save
```

## Continuous MJPEG Output

This writes a continuous stream of JPEG frames:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 30 -n 100 -o mjpg_640x480.mjpg
```

This is useful for bandwidth tests, but the file is not a single JPEG image.

## MJPEG Save Frames Test

For `MJPG`/`MJPEG`, each captured V4L2 frame is already JPEG data. Save individual frames like this:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 10 --save-frames /tmp/frames
```

Expected output:

```text
/tmp/frames/frame_001.jpg
/tmp/frames/frame_002.jpg
/tmp/frames/frame_003.jpg
...
```

This is the direct preparation step for HTTP MJPEG streaming, because the stream will send these JPEG frames continuously to browser clients.

## Recommended Test Order

```bash
./ov5640_capture --list-formats -d /dev/video1
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 30 -n 100 --no-save
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 10 --save-frames /tmp/frames
ls -lh /tmp/frames
```

## HTTP MJPEG Streaming

This stage uses a built-in minimal HTTP server. It does not require Nginx, FFmpeg, RTSP, or external services.

Run on the board:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

Then open this URL on a PC browser in the same network:

```text
http://BOARD_IP:8080/stream
```

The HTTP service runs as a long-lived camera service. One capture thread keeps
reading MJPEG frames from V4L2 and stores the newest frame in shared memory.
Each HTTP request is handled by its own client thread, so `/stream`, `/snapshot`,
and `/metrics` can be requested while the service keeps capturing.

```text
capture_thread -> latest JPEG frame
                 -> client_thread: /stream
                 -> client_thread: /snapshot
                 -> client_thread: /metrics
```

The built-in HTTP server recognizes these endpoints:

```text
GET /snapshot
GET /metrics
```

Examples:

```bash
curl http://BOARD_IP:8080/snapshot -o snapshot.jpg
curl http://BOARD_IP:8080/metrics
```

With `-n 0`, the HTTP server keeps running and can handle repeated `/stream`,
`/snapshot`, and `/metrics` requests. Multiple clients can connect at the same
time, but the i.MX6ULL should still be tested with a small number of clients
first because CPU, USB, and network resources are limited.

ADB port forwarding example:

```bash
adb forward tcp:8080 tcp:8080
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

Multi-client check:

```text
1. Open http://127.0.0.1:8080/stream in the Ubuntu browser.
2. Keep the stream open.
3. Run curl http://127.0.0.1:8080/metrics in another Ubuntu terminal.
4. Run curl http://127.0.0.1:8080/snapshot -o snapshot.jpg.
5. Optionally open a second browser tab for /stream.
```

Next planned stage:

```text
LCD framebuffer preparation / JPEG decode experiments
```

## Frame Queue Test

The frame queue is the preparation step for multithreaded capture and streaming.

Run this test on the Ubuntu host:

```bash
make test-frame-queue
```

Expected output:

```text
frame_queue_test passed
```

The test verifies that a fixed-size queue drops the oldest frame when full and keeps the newest frames for consumers.

## LCD Framebuffer Test

This stage verifies the local LCD output path before JPEG decoding is added.
It opens `/dev/fb0`, reads framebuffer information, maps the LCD memory, and
writes a color-bar test pattern.

Run on the board:

```bash
./ov5640_capture --fb-test /dev/fb0
```

Expected result:

```text
Framebuffer: /dev/fb0
Resolution: WIDTHxHEIGHT, virtual: WIDTHxHEIGHT
Line length: N bytes, bpp: 16 or 32, memory: N bytes
Framebuffer test pattern written
```

The LCD should show color bars. This does not use the camera yet. The next LCD
stage is:

```text
MJPEG frame -> JPEG decode -> RGB565 -> framebuffer
```

## LCD Framebuffer Preview

This experiment displays camera frames on the LCD framebuffer.

YUYV preview does not need extra libraries:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f YUYV -r 5 -n 30 --fb-preview /dev/fb0
```

Pipeline:

```text
V4L2 YUYV frame -> RGB565 conversion -> framebuffer scaling -> LCD
```

MJPG preview needs JPEG decoding. Build with libjpeg support:

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1
```

If the toolchain has only a shared `libjpeg.so`, disable static linking:

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 STATIC=0
```

If libjpeg is installed in a custom sysroot path:

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 \
  JPEG_LIBS="-L/path/to/arm/sysroot/usr/lib -ljpeg"
```

Then run:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 30 --fb-preview /dev/fb0
```

Pipeline:

```text
V4L2 MJPG frame -> JPEG decode -> RGB565 -> framebuffer scaling -> LCD
```

HTTP MJPEG streaming can still use MJPG directly because browsers decode JPEG
frames themselves. LCD framebuffer cannot display compressed MJPG bytes directly.

If linking fails with `cannot find -ljpeg`, the ARM cross toolchain does not
currently provide libjpeg. Enable `jpeg` or `libjpeg-turbo` in Buildroot, rebuild
the SDK/sysroot, then build this project again with `USE_LIBJPEG=1`.

## HTTP MJPEG + LCD Preview

After building with JPEG decode support, the service can stream MJPG over HTTP
and display the same camera frames on the LCD:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0
```

Pipeline:

```text
V4L2 MJPG capture
  -> HTTP MJPEG /stream, /snapshot, /metrics
  -> JPEG decode -> RGB565 -> LCD framebuffer
```

Check metrics:

```bash
curl http://127.0.0.1:8080/metrics
```

Important fields:

```text
lcd_preview=enabled
lcd_frames=N
lcd_errors=0
```

## Basic Motion Detection

Stage 9 adds lightweight image processing on decoded MJPG frames. It computes
average brightness and a sampled frame-difference value, then exposes the result
through `/metrics`.

Run with HTTP service, LCD preview, and motion detection:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0 \
  --motion-detect --motion-threshold 15 --motion-dir /tmp/motion
```

Check:

```bash
curl http://127.0.0.1:8080/metrics
ls -lh /tmp/motion
```

Important fields:

```text
motion_detect=enabled
processed_frames=N
brightness=N
motion_delta=N
motion_max_delta=N
motion_detected=0 or 1
motion_active_frames=N
motion_events=N
motion_snapshots=N
motion_errors=0
```

This is not AI detection. It is a simple frame-difference baseline used to learn
image processing, event metrics, and event-triggered snapshots.

`motion_detected` is the current frame state. If motion happened a moment ago and
then stopped, `motion_detected` may return to 0. Use `motion_events`,
`motion_snapshots`, `motion_active_frames`, and `motion_max_delta` to observe
historical activity.

## HTTP MJPEG + LCD Preview

After building with JPEG decode support, the same MJPG camera stream can drive
both the HTTP service and the local LCD preview:

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0
```

Pipeline:

```text
V4L2 MJPG frame
  -> HTTP MJPEG /stream, /snapshot, /metrics
  -> JPEG decode -> RGB565 -> /dev/fb0
```

On the Ubuntu host:

```bash
adb forward tcp:8080 tcp:8080
curl http://127.0.0.1:8080/metrics
```

Metrics should include:

```text
lcd_preview=enabled
lcd_frames=N
```

After a dynamic build, inspect runtime dependencies:

```bash
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 STATIC=0 runtime-libs
```

This prints the ELF interpreter, `NEEDED` libraries, and where they are found in
the cross sysroot. If the board reports a missing `libjpeg.so.*` at runtime, copy
the ARM library from the sysroot to the board:

```bash
find $(arm-buildroot-linux-gnueabihf-gcc -print-sysroot) -name 'libjpeg.so*'
adb push /path/to/libjpeg.so.X /usr/lib/
```
