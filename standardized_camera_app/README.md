# i.MX6ULL Camera Vision Gateway

基于 100ASK i.MX6ULL 开发板的嵌入式 Linux 摄像头视觉网关项目。项目使用 C 语言实现 USB UVC 摄像头采集、HTTP MJPEG 推流、LCD framebuffer 本地预览、运行状态监控、运动检测和事件抓拍，适合作为嵌入式 Linux、音视频采集、基础图像处理和边缘视觉网关方向的学习与展示项目。

当前项目主线不是追求复杂 AI 推理，而是在 i.MX6ULL 这种资源受限平台上，完成一条稳定、可解释、可验证的轻量级视频网关链路。

```text
USB UVC Camera
  -> V4L2 mmap MJPG Capture
  -> HTTP MJPEG /stream
  -> /snapshot and /metrics
  -> JPEG Decode
  -> RGB565 Framebuffer LCD Preview
  -> Motion Detection and Motion Mask
  -> Motion Event Snapshots
```

## Highlights

### 1. 多线程 HTTP 视频服务

项目采用“采集线程 + 多客户端请求线程”的服务模型：

```text
capture_thread
  -> latest MJPG frame
      -> client_thread: /stream
      -> client_thread: /snapshot
      -> client_thread: /metrics
```

核心价值：

- V4L2 采集和 HTTP 客户端访问解耦。
- `/stream` 是长连接视频流，不会阻塞 `/snapshot` 和 `/metrics`。
- 浏览器播放视频时，仍然可以通过 curl 查询状态或抓取快照。
- 适合解释嵌入式网络服务中的线程拆分、共享数据和资源同步。

### 2. MJPG 双路输出链路

同一帧 MJPG 数据同时服务网络端和本地端：

```text
MJPG frame
  -> HTTP: directly send compressed JPEG frame
  -> LCD: JPEG decode -> RGB565 -> /dev/fb0
```

核心价值：

- HTTP 推流端直接发送摄像头输出的压缩 JPEG 帧，不做重复编码。
- LCD framebuffer 端必须解码成真实像素后才能显示。
- 体现了“压缩视频流”和“显示像素缓冲区”的区别。

### 3. 基于实验的格式选择

项目对比了 YUYV 和 MJPG 在当前硬件上的实际表现：

- YUYV 数据量大，在当前 USB 摄像头和 i.MX6ULL 组合下容易超时或卡顿。
- MJPG 是摄像头端压缩后的 JPEG 帧，USB 传输压力更低。
- 最终确定 `640x480 MJPG @ 15fps` 作为当前稳定主线。

这不是简单“支持多格式”，而是通过实验数据选择适合硬件条件的工程路线。

### 4. LCD 本地预览和 HTTP 推流同时运行

项目支持一边在浏览器查看 HTTP MJPEG 视频流，一边在开发板 LCD 上显示摄像头画面：

```text
browser /stream
board LCD /dev/fb0
curl /metrics
curl /snapshot
```

这让项目更接近真实网关设备：既能远程查看，也能本地显示和调试。

### 5. 轻量级运动检测和红色运动掩膜

i.MX6ULL 没有 NPU，不适合直接做 YOLO 这类实时 AI 推理。因此项目采用传统帧差法实现轻量运动检测：

```text
current frame
  -> RGB565 luma sampling
  -> compare with previous frame
  -> grid-based motion blocks
  -> red motion mask on LCD
  -> event snapshot
```

核心价值：

- 适合资源受限平台。
- 可以解释基本图像处理思路。
- 能形成“检测到运动 -> 标注区域 -> 保存事件图片 -> metrics 上报”的闭环。

### 6. 工程化构建和部署

项目包含：

- Makefile 交叉编译。
- 可选 libjpeg 动态链接。
- ADB 推送部署。
- runtime-libs 运行时依赖检查。
- GitHub 仓库整理和项目文档。

## Architecture

### 系统架构

```text
+-------------------+
| USB UVC Camera    |
+---------+---------+
          |
          v
+-------------------+
| V4L2 mmap Capture |
| /dev/video1       |
+---------+---------+
          |
          v
+-------------------+
| Latest MJPG Frame |
| shared by service |
+----+---------+----+
     |         |
     |         +--------------------------+
     |                                    |
     v                                    v
+------------+                    +----------------+
| HTTP Server|                    | JPEG Decoder   |
| /stream    |                    | MJPG -> RGB565 |
| /snapshot  |                    +--------+-------+
| /metrics   |                             |
+-----+------+                             v
      |                           +----------------+
      v                           | Motion Detect  |
+-------------+                   | Motion Mask    |
| Browser/curl|                   +--------+-------+
+-------------+                            |
                                           v
                                  +----------------+
                                  | Framebuffer    |
                                  | /dev/fb0 LCD   |
                                  +----------------+
```

### 线程模型

HTTP 服务模式下主要线程关系：

```text
main thread
  -> accept HTTP request
  -> create client_thread for each request

capture_thread
  -> VIDIOC_DQBUF
  -> copy latest MJPG frame
  -> update metrics
  -> optional JPEG decode
  -> optional LCD preview
  -> optional motion detection
  -> VIDIOC_QBUF

client_thread
  -> /stream: continuously send latest MJPG frames
  -> /snapshot: send one latest JPEG frame
  -> /metrics: send runtime status text
```

### 数据流

HTTP 推流数据流：

```text
V4L2 DQBUF
  -> MJPG frame
  -> latest frame buffer
  -> HTTP multipart/x-mixed-replace
  -> browser
```

LCD 预览数据流：

```text
V4L2 DQBUF
  -> MJPG frame
  -> libjpeg decode
  -> RGB565 pixels
  -> framebuffer scaling
  -> /dev/fb0
```

运动检测数据流：

```text
RGB565 frame
  -> luma sampling
  -> compare with previous frame
  -> 64x48 motion grid
  -> active blocks
  -> red motion mask
  -> motion event snapshot
```

## Validation Results

### 1. 设备识别

当前硬件环境中：

```text
/dev/video1 -> USB UVC Camera
/dev/video0 -> pxp device, not the camera
```

运行前建议确认：

```bash
./ov5640_capture --list-formats -d /dev/video1
```

### 2. 格式和分辨率实验结论

| 格式 | 分辨率 | 请求 FPS | 实测现象 | 结论 |
| --- | --- | --- | --- | --- |
| MJPG | 640x480 | 15 | 稳定，适合推流和 LCD 预览 | 当前主线 |
| MJPG | 640x480 | 30 | 可运行，实际帧率受硬件和处理链路影响 | 可测试 |
| MJPG | 1280x720 | 30 | 帧率明显下降，可能出现 dropped buffers | 不作为主线 |
| YUYV | 640x480 | 5/30 | 容易超时，不稳定 | 仅保留为实验格式 |

最终推荐：

```text
device: /dev/video1
format: MJPG
size:   640x480
fps:    15
```

### 3. 多客户端并发验证

验证方式：

```bash
adb forward tcp:8080 tcp:8080
```

同时执行：

```text
1. 浏览器打开 http://127.0.0.1:8080/stream
2. curl http://127.0.0.1:8080/metrics
3. curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
4. 可选：第二个浏览器标签页打开 /stream
```

预期：

```text
/stream 持续播放
/metrics 可立即返回
/snapshot 可保存当前 JPEG 帧
connected_clients 和 total_clients 正常变化
```

### 4. 综合网关场景验证

运行完整网关：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0 \
  --motion-detect --motion-threshold 15 --motion-dir /tmp/motion
```

验证点：

- 浏览器可以查看 `/stream`。
- LCD 可以显示摄像头画面。
- 画面运动时 LCD 出现红色运动区域。
- `/metrics` 输出运动检测指标。
- `/tmp/motion` 保存运动事件快照。

关键 metrics：

```text
connected_clients=N
captured_frames=N
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

### 5. 硬件边界结论

- i.MX6ULL 适合做采集、轻量推流、本地显示和简单图像处理。
- i.MX6ULL 不适合本地实时 H.264 软件编码和 YOLO 类 AI 推理。
- LCD 预览需要 JPEG 软件解码，CPU 压力比 HTTP MJPEG 直接推流更高。
- RTSP/H.264 更适合作为后续在 RK3588、PC 或带硬件编码平台上的扩展方向。

## Build

基础静态构建，不启用 JPEG 解码：

```bash
cd ~/Videos/cam-1-capture10frames-main/standardized_camera_app
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
```

启用 libjpeg，用于 MJPG LCD 预览和运动检测：

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 STATIC=0
```

输出文件：

```text
build/ov5640_capture
```

部署到开发板：

```bash
adb push build/ov5640_capture /root/
adb shell
cd /root
chmod +x ov5640_capture
```

检查动态库依赖：

```bash
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf- USE_LIBJPEG=1 STATIC=0 runtime-libs
```

## Quick Start

列出摄像头格式：

```bash
./ov5640_capture --list-formats -d /dev/video1
```

只采集并统计，不保存文件：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 100 --no-save
```

保存 MJPG 单帧：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 10 --save-frames /tmp/frames
```

HTTP MJPEG 推流：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

HTTP 推流 + LCD 预览：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0
```

HTTP 推流 + LCD 预览 + 运动检测：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 \
  --http-mjpeg 8080 --fb-preview /dev/fb0 \
  --motion-detect --motion-threshold 15 --motion-dir /tmp/motion
```

Ubuntu 主机访问：

```bash
adb forward tcp:8080 tcp:8080
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

浏览器访问：

```text
http://127.0.0.1:8080/stream
```

## HTTP API

内置 HTTP 服务不依赖 Nginx、FFmpeg、RTSP 或外部服务器。

```text
GET /stream    MJPEG 视频流
GET /snapshot  最新 JPEG 快照
GET /metrics   文本状态指标
```

## Motion Threshold

运动检测使用传统帧差法，不是 AI 目标检测。

推荐阈值：

```text
8   灵敏，适合功能验证，但容易误检
15  推荐起点
20  更稳定，适合普通场景
30  保守，只检测明显运动
```

如果静止画面也频繁标红，提高 `--motion-threshold`。如果明显运动没有标红，降低该参数。

## Directory Layout

```text
standardized_camera_app/
  include/       头文件
  src/           源码
  tests/         主机侧测试
  scripts/       构建和运行辅助脚本
  docs/          项目计划、进度和学习文档
  Makefile
  README.md
```

## Source Modules

### src/main.c

程序入口。负责解析命令行参数、初始化 `camera_config_t`、打印运行配置，并根据参数选择执行格式枚举、framebuffer 测试、普通采集、HTTP 推流、LCD 预览或运动检测模式。

### src/v4l2_capture.c

项目核心模块。负责 V4L2 摄像头打开、格式配置、mmap buffer 申请、QBUF/DQBUF 采集流程、采集统计、文件保存、HTTP 服务模式下的采集线程、LCD 预览、运动检测和事件快照。当前该文件承担职责较多，后续可拆分为 `motion_detect.c`、`http_service.c`、`app_service.c` 等模块。

### src/camera_device.c

摄像头设备能力枚举模块。负责通过 V4L2 ioctl 查询设备驱动信息、支持的像素格式、分辨率和帧率，用于 `--list-formats`。

### src/stream_http_mjpeg.c

轻量 HTTP MJPEG 服务基础模块。负责 socket 监听、accept 请求、解析请求路径、发送 multipart MJPEG 响应、发送 JPEG 快照响应和文本响应。

### src/framebuffer_display.c

LCD framebuffer 显示模块。负责打开 `/dev/fb0`、获取 framebuffer 参数、mmap 显存、按 RGB bitfield 打包颜色、绘制测试彩条、将 RGB565 图像缩放写入 LCD。

### src/jpeg_decoder.c

JPEG 解码模块。启用 `USE_LIBJPEG=1` 时使用 libjpeg 将 MJPG/JPEG 压缩帧解码为 RGB565；未启用 libjpeg 时提供 stub，避免基础构建依赖 JPEG 库。

### src/frame_queue.c

环形帧队列模块。用于学习生产者消费者模型，支持固定容量、满队列丢弃旧帧、线程同步和关闭通知。当前不链接进主网关程序，但保留 `make test-frame-queue` 作为学习和测试模块。

## Header Modules

### include/camera_capture.h

定义摄像头配置结构体、采集上下文、mmap buffer 信息，以及主采集流程对外接口。

### include/camera_device.h

声明摄像头格式枚举接口。

### include/stream_http_mjpeg.h

声明 HTTP MJPEG 服务基础接口，包括监听、关闭、发送 stream header、发送 JPEG 帧、发送快照和文本响应。

### include/framebuffer_display.h

声明 framebuffer 打开、关闭、RGB565 绘制和测试彩条接口。

### include/jpeg_decoder.h

声明 JPEG 到 RGB565 的解码接口。

### include/frame.h

定义通用视频帧结构 `frame_t`，包含数据指针、长度、序号、时间戳和像素格式。

### include/frame_queue.h

声明环形帧队列接口，用于生产者消费者模型测试。

## Tests

frame_queue 是学习和测试模块，不属于主网关二进制。

```bash
make test-frame-queue
```

预期输出：

```text
frame_queue_test passed
```

## Known Limits

- 当前主线验证格式为 `640x480 MJPG`。
- YUYV 在当前硬件组合下不稳定，仅保留为实验格式。
- 运动检测是传统帧差法，不是 AI 目标检测。
- i.MX6ULL 无 NPU，不适合实时 YOLO 类推理。
- RTSP/H.264 建议作为后续在 RK3588、PC 或硬件编码平台上的扩展。

## Documents

- `docs/full_project_plan.md`：完整项目计划
- `docs/progress.md`：实验和开发进度
- `docs/learning_roadmap.md`：学习路线
- `docs/git_workflow.md`：Git 建仓和管理流程
