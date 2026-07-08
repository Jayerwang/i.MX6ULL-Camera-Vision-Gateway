# i.MX6ULL Camera Vision Gateway 项目进度

更新时间：2026-07-08

## 当前结论

项目已经从最初的 V4L2 采集 demo，推进到一个小型嵌入式视觉网关雏形。

当前主线能力：

```text
V4L2 MJPG 采集
  -> frame_queue 环形帧队列
  -> pthread 多线程
  -> HTTP MJPEG 推流
  -> /snapshot 快照接口
  -> /metrics 指标接口
  -> 长运行 HTTP 请求循环
```

当前推荐硬件参数：

```text
device: /dev/video1
format: MJPG
size:   640x480
fps:    15
```

## 已验证硬件状态

- `/dev/video1` 是可用 USB UVC 摄像头。
- `/dev/video0` 是 `pxp`，不是普通摄像头采集节点。
- `640x480 MJPG` 是当前最稳定主线。
- `1280x720 MJPG` 可以作为后续优化实验，不阻塞主线。
- `YUYV` 在当前板子/摄像头组合下不稳定，暂不作为主线。

## 已完成阶段

### 1. 标准化工程结构

已完成：

- `include/`
- `src/`
- `docs/`
- `scripts/`
- `output/`
- `Makefile`
- `.gitignore`
- GitHub 仓库同步

GitHub 仓库：

```text
https://github.com/Jayerwang/i.MX6ULL-Camera-Vision-Gateway.git
```

### 2. V4L2 采集能力

已完成：

- V4L2 mmap 采集
- `--list-formats`
- `--no-save`
- `--save-frames DIR`
- MJPG/MJPEG 支持
- 采集 fps、字节数、超时数、空帧数统计

### 3. HTTP MJPEG 单客户端推流

已完成并验证：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

Ubuntu 浏览器访问成功：

```text
http://127.0.0.1:8080/stream
```

链路已跑通：

```text
USB camera
  -> UVC driver
  -> V4L2 mmap
  -> MJPG frame
  -> built-in HTTP server
  -> HTTP MJPEG
  -> browser
```

### 4. frame_queue 环形帧队列

已完成：

- `include/frame.h`
- `include/frame_queue.h`
- `src/frame_queue.c`
- `tests/frame_queue_test.c`

已验证：

```bash
make test-frame-queue
```

输出：

```text
frame_queue_test passed
```

队列策略：

```text
容量固定
满了丢旧帧
保留最新帧
统计 dropped_frames
```

### 5. pthread 多线程采集和推流

已完成：

```text
capture_thread
  -> frame_queue
  -> sender_thread
  -> browser
```

线程职责：

- `capture_thread`：负责 V4L2 `DQBUF/QBUF`，把 MJPG 帧放入队列。
- `sender_thread`：负责从队列取帧，通过 HTTP MJPEG 发给浏览器。
- `main thread`：负责启动线程、等待线程、清理资源。

这体现了生产者消费者模型：

```text
生产者：capture_thread
缓冲区：frame_queue
消费者：sender_thread
```

### 6. 基础网关 API

已完成：

```text
GET /stream
GET /snapshot
GET /metrics
```

测试方式：

```bash
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
curl http://127.0.0.1:8080/metrics
```

注意：`curl ...` 是 Ubuntu 终端命令，不要输入到 Firefox 地址栏。

已验证结果：

```bash
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

结果：

```text
下载成功，snapshot.jpg 大小约 35676 bytes
```

```bash
curl http://127.0.0.1:8080/metrics
```

结果：

```text
device=/dev/video1
format=MJPG
width=640
height=480
fps_request=15
mode=single-request
```

说明：第一次 `/metrics` 出现 `Connection reset by peer`，是旧版“一次启动处理一个请求”导致的连接关闭现象，不是摄像头或网络故障。后续长运行 HTTP 服务会修复这个使用体验。

### 7. 长运行 HTTP 服务循环

已完成本轮代码推进：

```text
程序启动一次
  -> listen 8080
  -> accept request
  -> 根据路径处理 /stream /snapshot /metrics
  -> 关闭当前 client
  -> 继续 accept 下一个 request
```

目标行为：

```text
-n 0 时，程序不再处理一个请求就退出
可以连续访问 /metrics、/snapshot、/stream
```

待板端验证。

## 当前测试方式

### 拉取代码

```bash
cd ~/Videos/cam-1-capture10frames-main/standardized_camera_app
git pull
```

### 本机队列测试

```bash
make test-frame-queue
```

### 交叉编译

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
adb push build/ov5640_capture /root/
```

### 板端运行

```bash
adb shell
cd /root
chmod +x ov5640_capture
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

### ADB 端口转发

在 Ubuntu 主机终端执行，不是在 `adb shell` 里执行：

```bash
adb forward tcp:8080 tcp:8080
```

### 推流测试

Ubuntu 浏览器打开：

```text
http://127.0.0.1:8080/stream
```

### 快照测试

```bash
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

或浏览器直接打开：

```text
http://127.0.0.1:8080/snapshot
```

### 指标测试

```bash
curl http://127.0.0.1:8080/metrics
```

## 本次快照测试问题说明

截图里出现 Google reCAPTCHA，不是板子网络不好，也不是摄像头程序问题。

原因是把下面这条命令输入到了 Firefox 地址栏：

```bash
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

浏览器把它当成搜索内容，跳到了 Google。正确做法是在 Ubuntu 终端执行这条命令。

## 当前限制

- 当前是长运行顺序服务，还不是同时多客户端服务。
- `/stream` 连接期间会占用当前请求处理流程，断开后才能处理下一个请求。
- 多个浏览器同时看 `/stream` 还未实现。
- `/metrics` 目前仍是基础配置指标，尚未包含完整实时统计。
- LCD 本地显示还未实现。
- JPEG 解码、RGB565 转换、图像处理还未实现。
- RTSP/H.264 仍属于后续研究方向。

## 接下来几步计划

### 下一步 1：验证长运行 HTTP 服务

目标：

```text
程序启动一次后，可以连续访问 /metrics、/snapshot、/stream。
```

验收：

```bash
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

不需要每次重新启动程序。

### 下一步 2：多客户端 HTTP MJPEG

目标：

```text
多个浏览器可以同时访问 /stream。
```

计划结构：

```text
capture_thread
  -> latest frame / frame queue
  -> client_thread_1
  -> client_thread_2
```

关键原则：

```text
慢客户端不能阻塞采集线程
慢客户端不能阻塞其他客户端
```

### 下一步 3：完善 metrics

目标：

```text
/metrics 返回更完整运行状态。
```

计划指标：

- capture fps
- sent frames
- dropped frames
- timeout count
- empty frame count
- connected clients
- last frame size

### 下一步 4：LCD 显示准备

目标：

```text
为 LCD framebuffer 显示做准备。
```

计划：

```text
MJPG/JPEG
  -> JPEG decode
  -> RGB888
  -> RGB565
  -> /dev/fb0
```

## 进度维护规则

以后每完成一个阶段，都要更新本文档：

```text
standardized_camera_app/docs/progress.md
```

每次更新至少包含：

- 完成了什么
- 怎么测试
- 测试结果
- 当前限制
- 下一步计划

## 2026-07-08：长运行 HTTP 服务验证通过，并推进多客户端结构

### 已完成验证

用户在 Ubuntu 主机通过 ADB 端口转发访问板端 HTTP 服务：

```bash
adb forward tcp:8080 tcp:8080
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot1.jpg
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot2.jpg
```

测试结果：

```text
/metrics 正常返回：
device=/dev/video1
format=MJPG
width=640
height=480
fps_request=15
mode=http-service

/snapshot 连续两次保存成功：
snapshot1.jpg: 38172 bytes
snapshot2.jpg: 38504 bytes
```

结论：

```text
HTTP 服务已经从“一次请求后退出”推进到“程序持续运行，可反复访问 /metrics 和 /snapshot”。
之前截图里出现 Google reCAPTCHA，不是摄像头程序和网络问题，而是把 curl 命令误输入到了浏览器地址栏。
```

### 本次代码推进

HTTP MJPEG 服务继续从顺序处理升级为多客户端结构：

```text
capture_thread
  -> 持续从 V4L2 采集 MJPEG
  -> 保存最新 JPEG 帧

client_thread
  -> 每个 HTTP 请求单独一个线程
  -> /stream 发送连续 MJPEG
  -> /snapshot 返回当前最新帧
  -> /metrics 返回服务状态
```

这一步解决的问题：

```text
/stream 长连接不再占住整个 HTTP 服务流程。
浏览器看视频流时，另一个终端仍然可以访问 /metrics 和 /snapshot。
后续可以继续扩展为多个浏览器同时看 /stream。
```

### 下一次板端验收

启动服务：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

Ubuntu 主机转发端口：

```bash
adb forward tcp:8080 tcp:8080
```

验收步骤：

```bash
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

然后保持浏览器打开：

```text
http://127.0.0.1:8080/stream
```

在另一个终端继续测试：

```bash
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot_while_stream.jpg
```

预期结果：

```text
浏览器 /stream 保持播放。
/metrics 能立即返回。
/snapshot 能保存图片。
/metrics 中 mode 应为 multi-client-service，并出现 connected_clients、captured_frames、latest_frame_size 等字段。
```

### 当前限制

- 多客户端代码已经加入，仍需要在 i.MX6ULL 板端交叉编译和实机验证。
- 当前架构保存的是“最新 JPEG 帧”，适合 HTTP MJPEG、snapshot 和轻量 API；还不是 RTSP/H.264。
- LCD 显示还未实现，下一阶段需要做 JPEG 解码、RGB565 转换和 framebuffer 输出实验。

## 2026-07-08：多客户端 HTTP MJPEG 实机验收通过

### 已完成验证

用户在 i.MX6ULL 板端启动服务：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

Ubuntu 主机通过 ADB 转发访问：

```bash
adb forward tcp:8080 tcp:8080
curl http://127.0.0.1:8080/metrics
curl http://127.0.0.1:8080/snapshot -o snapshot.jpg
```

同时在浏览器打开：

```text
http://127.0.0.1:8080/stream
```

### 测试结果

服务端持续采集，浏览器可以看到实时 MJPEG 画面。期间继续访问 `/metrics` 和 `/snapshot` 都可以成功返回。

关键返回值：

```text
device=/dev/video1
format=MJPG
width=640
height=480
fps_request=15
mode=multi-client-service
connected_clients=2
total_clients=5
captured_frames=1607
latest_sequence=1625
latest_frame_size=49648
timeouts=0
empty_frames=0
total_bytes=68679644
```

连续 snapshot 成功：

```text
snapshot.jpg: 约 38 KB 到 44 KB
curl 下载速度约 1 MB/s 以上
```

### 结论

```text
多客户端 HTTP 服务已经跑通：
/stream 长连接播放时，/metrics 和 /snapshot 不再被阻塞。
采集线程持续运行，HTTP 客户端请求由独立 client_thread 处理。
```

当前结构已经体现：

```text
capture_thread
  -> latest JPEG frame
  -> client_thread(/stream)
  -> client_thread(/snapshot)
  -> client_thread(/metrics)
```

### 当前限制

- 当前 `/stream` 是 HTTP MJPEG，不是 RTSP/H.264。
- 当前画面直接使用摄像头输出的 MJPEG，不做 JPEG 解码和图像处理。
- LCD 本地显示还没做，需要进入 JPEG 解码、RGB565 转换、framebuffer 显示阶段。
- 多客户端已经验证可用，但 i.MX6ULL 资源有限，后续需要测试 2 个、3 个浏览器同时连接时的 CPU 占用和实际帧率。

### 下一步计划

进入 LCD 显示准备阶段：

```text
MJPEG camera frame
  -> JPEG decode
  -> RGB888 / RGB565
  -> /dev/fb0 framebuffer
```

同时补充运行状态指标：

```text
capture_fps
stream_clients
snapshot_count
metrics_count
last_error
```

## 2026-07-08：LCD framebuffer 测试模块加入

### 本次完成

加入 LCD framebuffer 显示准备模块：

```text
include/framebuffer_display.h
src/framebuffer_display.c
```

新增命令：

```bash
./ov5640_capture --fb-test /dev/fb0
```

当前功能：

```text
打开 /dev/fb0
读取 fb_fix_screeninfo
读取 fb_var_screeninfo
mmap framebuffer 显存
支持 16bpp RGB565 色条
支持 32bpp XRGB8888 色条
写入 LCD 测试图案
```

### 为什么先做这个

LCD 实时显示摄像头画面的完整链路是：

```text
MJPEG camera frame
  -> JPEG decode
  -> RGB888 / RGB565
  -> /dev/fb0 framebuffer
```

其中 `/dev/fb0` 是最后的输出设备。如果不先验证 framebuffer 能打开、能 mmap、能写屏，后面 JPEG 解码出了图也不知道是解码问题还是显示问题。

所以本阶段先把 LCD 输出通道单独验证出来。

### 下一次板端验收

交叉编译：

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
adb push build/ov5640_capture /root/
```

板端运行：

```bash
adb shell
cd /root
chmod +x ov5640_capture
./ov5640_capture --fb-test /dev/fb0
```

预期：

```text
终端打印 framebuffer 分辨率、line_length、bpp、显存大小。
LCD 出现彩色测试条。
```

如果失败，优先检查：

```bash
ls -lh /dev/fb*
cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/graphics/fb0/bits_per_pixel
```

### 当前限制

- 还没有把摄像头画面显示到 LCD。
- 还没有 JPEG 解码。
- 还没有图像缩放和居中显示。
- 如果 framebuffer 是 24bpp 或特殊像素格式，当前测试程序会提示不支持。

### 下一步计划

1. 在板端验证 `--fb-test /dev/fb0`。
2. 如果 LCD 色条通过，加入 JPEG 解码模块。
3. 实现 `MJPG -> RGB565`。
4. 把最新摄像头帧输出到 LCD。

## 2026-07-08：LCD framebuffer 写入成功但屏幕未显示，增强诊断

### 现象

用户在板端运行：

```bash
./ov5640_capture --fb-test /dev/fb0
```

程序输出：

```text
Framebuffer: /dev/fb0
Resolution: 1024x600, virtual: 1024x600
Line length: 4096 bytes, bpp: 32, memory: 33554432 bytes
Framebuffer test pattern written
```

这说明：

```text
/dev/fb0 可以打开
framebuffer 参数可以读取
mmap 成功
程序已经向显存写入数据
```

但 LCD 未显示色条。

### 可能原因

1. 32bpp 的真实颜色格式不是固定 XRGB8888，可能是 BGR、ARGB 或其他 bitfield。
2. LCD framebuffer 可能处于 blank 状态，需要 FBIOBLANK unblank。
3. `/dev/fb0` 可能不是实际可见的 LCD 层，或者显示控制器当前没把该层输出到面板。
4. LCD 背光、面板驱动、设备树或显示时序可能没有正确启用。
5. 当前系统上可能有其他显示程序或控制台覆盖了 framebuffer。

### 本次修正

增强 `src/framebuffer_display.c`：

```text
按 fb_var_screeninfo.red/green/blue/transp 的 offset 和 length 打包像素
打印颜色 bitfield 信息
调用 FBIOBLANK 解除 blank
写入后调用 msync 同步显存
```

下一次测试时重点观察新增输出：

```text
Color offsets: R?:? G?:? B?:? A?:?
Offset: x=?, y=?
```

### 下一次排查命令

如果仍不显示，板端执行：

```bash
ls -lh /dev/fb*
cat /sys/class/graphics/fb0/name
cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/graphics/fb0/bits_per_pixel
cat /sys/class/graphics/fb0/blank
```

如果有多个 framebuffer：

```bash
./ov5640_capture --fb-test /dev/fb1
```

如果 `/sys/class/graphics/fb0/blank` 是 `1` 或更高，可以尝试：

```bash
echo 0 > /sys/class/graphics/fb0/blank
```

## 2026-07-08：LCD YUYV 预览实验加入

### 前置结果

用户反馈 LCD 色条已经显示，说明：

```text
/dev/fb0 可用
framebuffer mmap 可用
写显存后 LCD 能显示
```

因此 LCD 输出通路已经打通。

### 本次完成

新增命令：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f YUYV -r 5 -n 30 --fb-preview /dev/fb0
```

实现链路：

```text
V4L2 DQBUF
  -> YUYV
  -> YUYV to RGB565
  -> framebuffer_display_draw_rgb565
  -> /dev/fb0
```

代码变化：

```text
camera_config_t 增加 fb_preview 和 fb_device
main.c 增加 --fb-preview 参数
framebuffer_display 增加可复用打开/关闭/绘制 RGB565 API
v4l2_capture.c 增加 YUYV LCD preview 采集循环
```

### 为什么先做 YUYV

当前稳定推流主线是 MJPG，但 MJPG 要上 LCD 必须先 JPEG 解码：

```text
MJPG -> JPEG decode -> RGB565 -> LCD
```

JPEG 解码依赖 libjpeg/libjpeg-turbo 或硬件解码器。为了避免一次引入太多变量，本阶段先使用 YUYV：

```text
YUYV -> RGB565 -> LCD
```

这样可以单独验证：

- V4L2 采集到 LCD 的数据通路
- 像素格式转换
- framebuffer 缩放写屏

### 下一次板端验收

交叉编译并推送：

```bash
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
adb push build/ov5640_capture /root/
```

板端运行：

```bash
cd /root
chmod +x ov5640_capture
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f YUYV -r 5 -n 30 --fb-preview /dev/fb0
```

预期：

```text
LCD 显示摄像头画面。
终端打印 Displayed frame N。
```

### 当前限制

- 由于之前 YUYV 在该摄像头上有过 timeout，不保证帧率稳定。
- 当前 YUYV 预览是实验链路，不是最终主线。
- 最终推荐仍是 MJPG 采集，再接 JPEG 解码显示到 LCD。

### 下一步计划

如果 YUYV LCD 预览能显示画面：

```text
加入 JPEG 解码模块
实现 MJPG -> RGB565
复用 framebuffer_display_draw_rgb565 输出到 LCD
```

如果 YUYV 仍然 timeout：

```text
直接进入 libjpeg-turbo/JPEG decode 方案
保持摄像头采集格式为 MJPG
```
