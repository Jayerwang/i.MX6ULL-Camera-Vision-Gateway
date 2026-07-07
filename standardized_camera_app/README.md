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

This first version supports one browser client at a time. Stop it with `Ctrl+C`.

Next planned stage:

```text
frame_queue + multi-client streaming
```
