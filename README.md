# i.MX6ULL Camera Vision Gateway

基于韦东山 / 100ask i.MX6ULL 开发板和 USB UVC 摄像头的嵌入式视觉网关学习项目。

当前项目从 V4L2 摄像头采集出发，逐步扩展到 MJPEG 推流、环形队列、多线程、多客户端、LCD 显示、图像处理、录像、HTTP API 和服务化运行。

## 当前主线

当前硬件实测后，推荐主线参数为：

```text
device: /dev/video1
format: MJPG
size:   640x480
fps:    15
```

`/dev/video0` 在当前板子上是 `pxp`，不是普通摄像头采集设备。

## 项目目录

```text
standardized_camera_app/
  include/      public headers
  src/          C source files
  docs/         project plans and learning notes
  scripts/      helper scripts
  output/       runtime outputs, ignored by git
  Makefile
  README.md
```

旧的原始实验工程保留在：

```text
imx6ull_camera_project/
```

主要开发请以 `standardized_camera_app` 为准。

## 编译

在 Ubuntu 虚拟机中：

```bash
cd ~/Videos/cam-1-capture10frames-main/standardized_camera_app
make clean
make CROSS_COMPILE=arm-buildroot-linux-gnueabihf-
```

生成文件：

```text
standardized_camera_app/build/ov5640_capture
```

## 部署到板子

```bash
adb push build/ov5640_capture /root/
adb shell
cd /root
chmod +x ov5640_capture
```

## 常用测试

列出摄像头支持格式：

```bash
./ov5640_capture --list-formats -d /dev/video1
```

只测试采集性能，不保存文件：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 100 --no-save
```

保存 MJPEG 帧为单独 JPEG 文件：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 10 --save-frames /tmp/frames
```

HTTP MJPEG 推流：

```bash
./ov5640_capture -d /dev/video1 -w 640 -h 480 -f MJPG -r 15 -n 0 --http-mjpeg 8080
```

浏览器打开：

```text
http://BOARD_IP:8080/stream
```

如果通过 ADB 转发：

```bash
adb forward tcp:8080 tcp:8080
```

然后浏览器打开：

```text
http://127.0.0.1:8080/stream
```

## 项目计划

完整计划书：

```text
standardized_camera_app/docs/full_project_plan.md
```

当前路线：

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

## License

MIT
