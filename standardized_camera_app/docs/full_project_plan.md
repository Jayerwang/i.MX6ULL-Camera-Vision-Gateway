# i.MX6ULL Camera Vision Gateway 完整项目计划书

## 1. 项目定位

本项目不是只做一个“采集 10 帧”的实验程序，而是以韦东山 i.MX6ULL 开发板和 USB UVC 摄像头为硬件基础，逐步扩展成一个小型嵌入式视觉网关项目。

最终目标：

```text
摄像头采集
  -> 帧缓存队列
  -> 图像处理
  -> 本地 LCD 显示
  -> HTTP MJPEG 推流
  -> Snapshot/API 网关
  -> 本地录像
  -> 服务化运行
  -> 后续 RTSP/H.264/AI 扩展
```

这个项目的学习重点不是“调用一个库把视频显示出来”，而是理解嵌入式 Linux 视频链路从设备、驱动、缓冲区、线程、网络到业务接口的完整工程设计。

## 2. 当前硬件和实验结论

硬件条件：

- 开发板：100ask / 韦东山 i.MX6ULL
- 摄像头：USB UVC 摄像头
- 可用摄像头节点：`/dev/video1`
- `/dev/video0`：`pxp`，不是普通摄像头采集设备
- LCD：板载 framebuffer，通常是 `/dev/fb0`

已经验证：

```text
/dev/video1
Driver: uvcvideo
Card: USB 2.0 Camera
Format:
  YUYV
  MJPG / Motion-JPEG
```

实验结论：

| 模式 | 实测表现 | 项目决策 |
| --- | --- | --- |
| `640x480 MJPG --no-save` | 约 13 到 22 fps，稳定 | 主线开发格式 |
| `640x480 MJPG --save-frames` | 约 12 fps，稳定 | 快照/保存帧基础 |
| `1280x720 MJPG` | 程序中较慢，`v4l2-ctl` 约 14 fps 但有 dropped buffers | 后续优化实验 |
| `640x480 YUYV` | 多次 timeout，不稳定 | 暂不作为主线 |

当前主线参数：

```text
device: /dev/video1
format: MJPG
size:   640x480
fps:    request 15
```

## 3. 已完成内容

当前 `standardized_camera_app` 已完成：

- 标准化目录结构
- `Makefile` 交叉编译
- V4L2 mmap 采集
- 命令行参数解析
- 设备格式枚举：`--list-formats`
- 支持 `YUYV` / `NV12` / `RGBP` / `MJPG` / `MJPEG` 参数解析
- 采集性能统计：
  - elapsed time
  - fps
  - total bytes
  - average frame size
  - timeout count
  - empty frame count
- `--no-save` 只测试采集性能
- `--save-frames DIR` 保存每帧 MJPEG 为 `.jpg`
- 第一版内置 HTTP MJPEG 推流：
  - `--http-mjpeg PORT`
  - 单客户端
  - MJPG/MJPEG only
  - multipart HTTP stream

## 4. 总体技术栈

### 4.1 Linux/V4L2 采集

必学内容：

- `/dev/videoX` 设备节点
- UVC 摄像头
- `ioctl`
- `VIDIOC_QUERYCAP`
- `VIDIOC_ENUM_FMT`
- `VIDIOC_S_FMT`
- `VIDIOC_S_PARM`
- `VIDIOC_REQBUFS`
- `VIDIOC_QUERYBUF`
- `mmap`
- `VIDIOC_QBUF`
- `VIDIOC_STREAMON`
- `select`
- `VIDIOC_DQBUF`
- `VIDIOC_QBUF`
- `VIDIOC_STREAMOFF`

项目中体现：

```text
src/v4l2_capture.c
include/camera_capture.h
```

### 4.2 图像格式

必学内容：

- YUYV：未压缩 YUV422，数据量大，CPU 处理简单，USB 带宽压力大
- MJPEG/MJPG：每帧是 JPEG 压缩图，USB 传输压力小，浏览器推流方便
- NV12：常见视频处理格式，适合编码器/硬件模块
- RGB565：适合 LCD framebuffer 显示
- RGB888：图像处理常用中间格式

当前建议：

```text
采集和推流：MJPG
LCD 显示：MJPG -> JPEG decode -> RGB888 -> RGB565 -> /dev/fb0
图像处理：MJPG -> JPEG decode -> RGB888/Gray
```

### 4.3 多线程和队列

必学内容：

- `pthread`
- `pthread_create`
- `pthread_join`
- `pthread_mutex_t`
- `pthread_cond_t`
- 生产者消费者模型
- 环形队列
- 最新帧覆盖策略
- 丢帧统计

目标架构：

```text
capture_thread
  -> frame_queue
  -> stream_thread
  -> http clients

capture_thread
  -> frame_queue
  -> process_thread
  -> display_thread / record_thread
```

为什么必须做：

摄像头采集、网络发送、LCD 显示、图像处理速度不同。如果全部放在一个循环里，任何一个环节变慢都会拖慢采集。队列和线程用于解耦不同模块的速度。

### 4.4 HTTP MJPEG 推流

必学内容：

- socket
- bind/listen/accept
- HTTP response header
- `multipart/x-mixed-replace`
- `Content-Type: image/jpeg`
- `Content-Length`
- 长连接连续发送 JPEG 帧

数据流程：

```text
UVC camera
  -> V4L2 mmap buffer
  -> MJPG frame
  -> HTTP multipart boundary
  -> browser
```

优点：

- 当前摄像头直接输出 MJPG
- 不需要 H.264 编码
- 不需要 FFmpeg/Nginx/RTSP server
- 浏览器可直接查看
- 非常适合当前 i.MX6ULL 阶段

限制：

- 带宽比 H.264 高
- 延迟和码率控制能力弱
- 工业 NVR/监控平台兼容性不如 RTSP/H.264

### 4.5 LCD framebuffer 显示

必学内容：

- `/dev/fb0`
- `fb_var_screeninfo`
- `fb_fix_screeninfo`
- framebuffer mmap
- RGB888 -> RGB565
- 图像缩放/居中/裁剪

由于当前主线采集格式是 MJPG，LCD 显示需要先解码：

```text
MJPG/JPEG
  -> JPEG decode
  -> RGB888
  -> RGB565
  -> framebuffer
```

推荐实现顺序：

1. 读取一张 `frame_001.jpg`
2. 解码成 RGB888
3. 转成 RGB565
4. 写入 `/dev/fb0`
5. 再接入实时采集线程

### 4.6 JPEG 解码

可选技术路线：

- `libjpeg` / `libjpeg-turbo`
- 自己只写调用层，不手写 JPEG 解码算法

模块设计：

```text
include/jpeg_codec.h
src/jpeg_codec.c
```

接口示例：

```c
int jpeg_decode_to_rgb888(const void *jpeg_data,
                          size_t jpeg_size,
                          unsigned char **rgb_data,
                          int *width,
                          int *height);
```

### 4.7 图像处理

第一阶段不要一上来做 AI，先做基础图像处理：

- RGB 转灰度
- 简单缩放
- 裁剪 ROI
- 亮度统计
- 帧差法运动检测
- 保存运动触发快照

数据流程：

```text
MJPG
  -> JPEG decode
  -> RGB/Gray
  -> image processing
  -> event/snapshot/metrics
```

### 4.8 网关 API

目标是让程序从“命令行采集 demo”升级成“设备服务”。

计划接口：

```text
GET /stream      HTTP MJPEG 实时视频
GET /snapshot    返回一张 JPEG
GET /metrics     返回运行指标
GET /status      返回设备状态
GET /config      查看当前配置
POST /config     修改部分配置
```

`/metrics` 示例：

```json
{
  "device": "/dev/video1",
  "format": "MJPG",
  "width": 640,
  "height": 480,
  "capture_fps": 13.8,
  "stream_clients": 1,
  "dropped_frames": 0,
  "timeouts": 0
}
```

### 4.9 本地录像

当前硬件建议从简单录像开始：

```text
MJPEG frame sequence
```

先不要直接做 MP4/H.264。因为 MP4 封装、时间戳、音视频同步、H.264 编码都会明显增加复杂度。

阶段功能：

- 按帧保存 JPEG
- 按时间创建目录
- 按时间分段
- 磁盘空间检查
- 自动删除旧文件

命令示例：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 --record-dir /root/record --segment-sec 60
```

### 4.10 服务化运行

成熟项目必须考虑长期运行。

需要补充：

- 配置文件
- 日志文件
- 信号处理
- 守护进程/后台运行
- systemd 或 init 脚本
- 崩溃自动重启
- 长时间稳定性测试

在 i.MX6ULL 的嵌入式系统上，不一定总是完整 systemd，也可能是 BusyBox init。因此服务脚本要根据实际系统选择。

## 5. 推荐项目目录

```text
standardized_camera_app/
  include/
    camera_capture.h
    camera_device.h
    frame.h
    frame_queue.h
    stream_http_mjpeg.h
    jpeg_codec.h
    framebuffer_display.h
    image_process.h
    metrics.h
    app_config.h

  src/
    main.c
    v4l2_capture.c
    camera_device.c
    frame_queue.c
    stream_http_mjpeg.c
    jpeg_codec.c
    framebuffer_display.c
    image_process.c
    metrics.c
    app_config.c

  docs/
    full_project_plan.md
    learning_roadmap.md
    improvement_plan.md

  scripts/
    deploy.sh
    run_capture.sh
    run_stream.sh

  output/
    # runtime output, ignored by git

  Makefile
  README.md
```

## 6. 分阶段实施计划

### 阶段 0：原始项目跑通

目标：

- 能编译
- 能运行
- 能采集
- 知道 `/dev/video1` 才是摄像头

验收：

```bash
./ov5640_capture --list-formats -d /dev/video1
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 100 --no-save
```

状态：已完成。

### 阶段 1：标准化工程结构

目标：

- 拆分 `main.c`
- 拆分 V4L2 采集模块
- 使用 `.h` 声明结构体和函数
- 使用 `Makefile`
- 加 `.gitignore`

状态：已完成大部分。

### 阶段 2：采集能力增强

目标：

- 支持列出格式
- 支持 MJPG
- 支持 no-save 性能测试
- 支持 save-frames 保存 JPEG
- 支持统计 fps/bytes/timeouts

状态：已完成。

### 阶段 3：HTTP MJPEG 单客户端推流

目标：

```text
浏览器打开 http://BOARD_IP:8080/stream 可以看到实时画面
```

当前命令：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

验收标准：

- 程序显示 `Waiting for HTTP client...`
- 浏览器连接后显示画面
- 程序持续输出采集帧
- `Ctrl+C` 能退出
- 浏览器断开后程序有明确提示

当前现象：

你已经跑到：

```text
HTTP MJPEG server listening on 0.0.0.0:8080
Waiting for HTTP client...
```

这说明服务器已经启动，正在等待浏览器连接。下一步不是改采集，而是解决访问路径：

- 确认板子 IP
- 确认 PC/虚拟机/板子网络互通
- 或使用 `adb forward tcp:8080 tcp:8080`

### 阶段 4：frame_queue 环形队列

目标：

```text
capture_thread 只负责采集，不被网络发送阻塞
```

新增模块：

```text
include/frame.h
include/frame_queue.h
src/frame_queue.c
```

核心结构：

```c
typedef struct {
    unsigned char *data;
    size_t size;
    int width;
    int height;
    unsigned int pixel_format;
    unsigned int sequence;
    unsigned long long timestamp_us;
} frame_t;
```

队列策略：

```text
容量固定
满了丢旧帧
保留最新帧
统计 dropped_frames
```

验收：

- 写一个生产者消费者测试
- push/pop 正常
- 队列满时不会崩溃
- dropped_frames 递增

### 阶段 5：多线程采集和推流

目标架构：

```text
main thread
  -> capture_thread
  -> http_server_thread
  -> client_thread
```

线程职责：

- `capture_thread`：V4L2 `DQBUF/QBUF`，把 MJPG 帧放入队列
- `http_server_thread`：监听端口，接受客户端
- `client_thread`：从最新帧缓存发送给浏览器

验收：

- 浏览器慢，不会卡死摄像头采集
- 采集 fps 和推流 fps 分开统计
- 单客户端稳定
- 为多客户端留出结构

### 阶段 6：多客户端 HTTP MJPEG

目标：

```text
多个浏览器同时打开 /stream
```

设计：

```text
capture_thread
  -> latest_frame_buffer
  -> client_thread_1
  -> client_thread_2
  -> client_thread_N
```

关键原则：

```text
慢客户端不能阻塞采集线程
慢客户端不能阻塞其他客户端
```

验收：

- 两个浏览器同时看
- 关闭一个浏览器不影响另一个
- `/metrics` 能看到客户端数量

### 阶段 7：Snapshot 和 Metrics API

目标：

```text
GET /snapshot 返回最新 JPEG
GET /metrics 返回运行指标
GET /status 返回状态
```

验收：

```bash
curl http://BOARD_IP:8080/snapshot -o snapshot.jpg
curl http://BOARD_IP:8080/metrics
```

### 阶段 8：LCD 本地显示

目标：

```text
LCD 显示摄像头实时画面
```

实现顺序：

1. 保存一张 MJPG 帧
2. 解码 JPEG 到 RGB888
3. RGB888 转 RGB565
4. 写 `/dev/fb0`
5. 接入实时队列

新增模块：

```text
include/jpeg_codec.h
src/jpeg_codec.c
include/framebuffer_display.h
src/framebuffer_display.c
```

验收：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 --preview-fb
```

### 阶段 9：基础图像处理

目标：

- 解码后做灰度图
- 做简单亮度统计
- 做帧差运动检测
- 运动触发保存快照

验收：

```text
画面变化明显时：
  motion_detected = true
  保存 snapshot
  metrics 记录事件次数
```

### 阶段 10：本地录像

目标：

- 按 JPEG 帧序列保存
- 分段
- 自动清理

验收：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 --record-dir /root/record --segment-sec 60
```

### 阶段 11：配置、日志和服务化

目标：

- 支持配置文件
- 支持日志文件
- 支持开机启动
- 支持异常退出清理资源

配置文件示例：

```ini
device=/dev/video1
width=640
height=480
format=MJPG
fps=15
http_port=8080
enable_stream=1
enable_snapshot=1
enable_lcd=0
```

验收：

```bash
./vision_gateway -c /etc/vision_gateway.conf
```

### 阶段 12：RTSP/H.264 研究线

RTSP/H.264 是工业场景常见方案，但当前不是主线立即实现项。

适合做 RTSP/H.264 的条件：

1. 摄像头直接输出 H.264
2. 板子有硬件 H.264 编码器
3. 分辨率和帧率很低，软件编码能跑得动
4. 项目迁移到 RK3568/RK3588 等更强平台

当前摄像头只看到：

```text
YUYV
MJPG
```

如果强行做 H.264：

```text
MJPG
  -> JPEG decode
  -> RGB/YUV
  -> H.264 encode
  -> RTP
  -> RTSP
```

这条链路对 i.MX6ULL 偏重。因此当前先做 HTTP MJPEG，把采集、队列、线程、网关 API、LCD、服务化这些基础打牢。

## 7. 工业场景功能清单

必须具备：

- 稳定采集
- 异常处理
- 日志
- 运行指标
- 网络推流
- 快照 API
- 配置文件
- 服务化启动
- 资源释放
- 长时间运行测试

建议具备：

- 多客户端
- 本地 LCD 预览
- 本地录像
- 磁盘空间管理
- 简单运动检测
- HTTP 控制接口

后续增强：

- MQTT 上报
- RTSP/H.264
- AI 推理
- Web 管理页面
- OTA/远程升级

## 8. 当前下一步

现在不要继续纠结 720p 或 YUYV。当前已经证明 `640x480 MJPG` 是最稳的主线。

下一步顺序：

1. 先把 HTTP MJPEG 浏览器访问跑通
2. 如果浏览器打不开，优先排查网络：
   - 板子 IP
   - PC 和板子是否同网段
   - `adb forward tcp:8080 tcp:8080`
   - 防火墙/NAT
3. 跑通后实现 `frame_queue`
4. 再拆成 `capture_thread + stream_thread`
5. 再做 `/snapshot` 和 `/metrics`
6. 然后进入 LCD 显示

## 9. 学习作业

### 作业 1：解释 HTTP MJPEG 当前状态

根据你刚才的输出，回答：

```text
HTTP MJPEG server listening on 0.0.0.0:8080
Waiting for HTTP client...
```

问题：

1. 这说明程序运行到了哪一步？
2. 为什么还没有显示 `Captured frame`？
3. 浏览器连接后程序会发生什么？
4. `0.0.0.0:8080` 和 `BOARD_IP:8080` 是什么关系？

### 作业 2：画数据流程图

画出：

```text
USB camera -> UVC driver -> V4L2 mmap -> MJPG frame -> HTTP multipart -> browser
```

### 作业 3：设计 frame_queue

写出你认为需要的结构体字段：

```c
typedef struct {
    ...
} frame_queue_t;
```

要求说明：

- 为什么要 mutex
- 为什么要 cond
- 为什么满了应该丢旧帧
- dropped_frames 放在哪里统计

## 10. 最终路线总结

当前项目主路线：

```text
640x480 MJPG
  -> HTTP MJPEG
  -> frame_queue
  -> pthread
  -> multi-client
  -> snapshot API
  -> metrics API
  -> LCD preview
  -> image processing
  -> recording
  -> config/log/service
  -> RTSP/H.264 research
```

一句话：

这个项目先用当前硬件能稳定跑起来的 `640x480 MJPG + HTTP MJPEG` 建立完整闭环，再逐步加入队列、多线程、LCD、图像处理和服务化能力，最后再研究 RTSP/H.264 这类更工业化但更重的方案。
