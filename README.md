# AUV-StreamCore

**AUV-StreamCore** 是一个专为 **Rockchip RK3588** 平台（如 Orange Pi 5 Pro）设计的高性能、低延迟视频推流引擎。

该项目充分利用了 Rockchip 的硬件加速能力，实现了从摄像头采集、色彩空间转换、H.264 编码到网络传输的全链路优化。

## 🚀 核心特性

*   **V4L2 采集**：直接操作底层 Video4Linux2 接口，获取原始 YUYV 图像数据。
*   **硬件加速转换 (RGA)**：使用 Rockchip **RGA (2D Graphic Acceleration)** 进行格式转换（YUYV -> NV12）和缩放。
    *   *亮点：使用 DMA Buffer 实现零拷贝 (Zero-Copy)，不占用 CPU。*
*   **硬件编码 (MPP)**：集成 Rockchip **MPP (Media Process Platform)**，实现 H.264 硬件编码。
    *   *亮点：支持 CBR (固定码率) 控制，直接输入 DMA FD，极大降低延迟。*
*   **UDP 低延迟传输**：通过原始 UDP Socket 发送 H.264 码流，极大减少传输开销。

## 🛠️ 硬件与环境要求

*   **硬件平台**：Orange Pi 5 / 5 Pro / 5 Plus (基于 RK3588/RK3588S)
*   **操作系统**：Ubuntu 20.04 / 22.04 (Rockchip BSP)
*   **依赖库**：
    *   `libv4l-dev`
    *   `librga-dev` (Rockchip RGA)
    *   `librockchip-mpp-dev` (Rockchip MPP)
    *   `opencv` (可选，目前仅用于辅助)

## 📂 项目结构

```text
.
├── CMakeLists.txt      # 构建脚本
├── inc/                # 头文件
│   ├── dma_utils.h     # DMA Buffer 管理
│   ├── mpp_encoder.h   # MPP 编码封装类
│   └── v4l2_utils.h    # V4L2 操作封装
├── src/                # 源代码
│   ├── dma_utils.cpp   # 实现 dma-heap 内存分配
│   ├── main.cpp        # 主程序入口 (采集->RGA->MPP->UDP)
│   ├── mpp_encoder.cpp # MPP 编码实现
│   └── v4l2_utils.c    # V4L2 采集实现
└── README.md           # 说明文档
```

## ⚙️ 编译与构建

确保你的系统环境已安装必要的开发库。

```code
# 1. 创建构建目录
mkdir build
cd build

# 2. 生成 Makefile
cmake ..

# 3. 编译
make -j4
```

## 🚀 运行指南

### 1. 接收端配置 (PC端)

在运行板子上的程序前，请先在 PC 端准备好接收工具。你可以使用 `ffplay` (FFmpeg 组件) 来播放。

**PC端命令 (Windows/Linux):**

```bash
# -f h264: 指定格式为 H.264
# -i udp://0.0.0.0:8888: 监听本地 8888 端口
# -fflags nobuffer: 禁用缓冲区以降低延迟
# -flags low_delay: 低延迟模式

ffplay -f h264 -fflags nobuffer -flags low_delay -framedrop -i udp://0.0.0.0:8888
```

### 2. 发送端配置 (Orange Pi)

修改 `src/main.cpp` 中的目标 IP，将其指向你的 PC：

```cpp
#define DEST_IP   "192.168.1.xxx" // <--- 修改为你 PC 的 IP 地址
#define DEST_PORT 8888            // <--- 确保没有被防火墙拦截
```

### 3. 开始推流

在板子上执行编译好的程序：

```bash
cd build
sudo ./auv_vision /dev/video0
```

如果一切正常，你应该能在 PC 上看到来自摄像头的实时画面，且延迟极低。

## 📝 关键参数说明

在 `src/main.cpp` 中可以调整以下宏定义：

*   `DST_WIDTH` / `DST_HEIGHT`: RGA 输出和 MPP 编码的分辨率（默认 640x640）。
*   `UDP_MTU`: UDP 分包大小（默认 1024），建议小于 MTU 1500。

在 `src/mpp_encoder.cpp` 中可以调整编码参数：

*   `bps` (Bitrate): 目标码率，当前设置为 2Mbps ~ 3Mbps。
*   `rc:mode`: 码率控制模式 (CBR 固定码率)。

## ⚠️ 常见问题排查

1.  **PC 端收不到画面**：
    *   检查 PC 的防火墙是否允许 UDP 8888 端口。
    *   检查 `main.cpp` 中的 `DEST_IP` 是否填写正确。
    *   确保 PC 和开发板在同一个局域网网段。

2.  **画面绿屏或花屏**：
    *   检查 MPP 的 stride (跨距) 设置。RK3588 通常要求宽和高对齐到 16 或 32。
    *   检查 RGA 转换时的格式是否设置为 `RK_FORMAT_YCbCr_420_SP` (即 NV12)。

3.  **Permission Denied (权限错误)**：
    *   操作 `/dev/video0` 和 `/dev/dma_heap` 通常需要 root 权限，请使用 `sudo` 运行。