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
