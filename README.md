# i.MX6ULL + OV5640 摄像头项目

基于正点原子 i.MX6ULL 阿尔法开发板 + OV5640 摄像头的 V4L2 图像采集项目。

## 📁 项目结构

```
imx6ull_camera_project/
├── app/                          # 用户态 V4L2 采集程序
│   ├── ov5640_capture.c          # 核心采集程序
│   └── Makefile                  # 交叉编译脚本
├── scripts/                      # 辅助脚本
│   └── yuv2jpg.py               # YUV → JPG 转换工具
├── imx6ull-alientek-emmc.dts    # 设备树（OV5640 节点）
└── README.md
```

## 🔧 编译

```bash
cd app
make
```


交叉编译工具链: `arm-linux-gnueabihf-gcc` (Linaro GCC 4.9.4)

## 🚀 使用

### 1. 加载驱动
```bash
modprobe ov5640_camera
modprobe ov5640_camera_int
```

### 2. 采集图像
```bash
# 采集 1 帧
./ov5640_capture -n 1 -o test.yuv

# 采集 10 帧
./ov5640_capture -n 10 -o test10.yuv

# 指定分辨率
./ov5640_capture -w 640 -h 480 -n 5 -o test.yuv
```

### 3. 查看图像（PC 端）
```bash
# 用 ffplay 直接播放
ffplay -f rawvideo -pixel_format yuyv422 -video_size 1024x600 output.yuv

# 或用 Python 转 JPG
python3 scripts/yuv2jpg.py output.yuv 1024 600
```

## 📋 命令参数

| 参数 | 说明                      | 默认值     |
| ---- | ------------------------- | ---------- |
| `-w` | 图像宽度                  | 1024       |
| `-h` | 图像高度                  | 600        |
| `-n` | 采集帧数 (0=无限)         | 10         |
| `-o` | 输出文件名                | output.yuv |
| `-f` | 像素格式 (YUYV/NV12/RGBP) | YUYV       |
| `-r` | 帧率                      | 30         |

## 🛠 内核配置

关键配置项：
- `CONFIG_MEDIA_SUPPORT=y`
- `CONFIG_MXC_CAMERA_OV5640=m`
- `CONFIG_VIDEO_MXC_CAPTURE=m`
- `CONFIG_VIDEO_MXC_CSI_CAMERA=m`
- `CONFIG_CMA_SIZE_MBYTES=128`

## 📝 硬件

- 主控: NXP i.MX6ULL (Cortex-A7)
- 摄像头: OV5640 (DVP 8bit 并行接口)
- 接口: I2C2 (地址 0x3c) + CSI
- LCD: 1024x600

## 📄 License

MIT
