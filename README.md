# VisionLink-RK

**VisionLink-RK** 是一个专为 **Rockchip RK3588** 平台（如 Orange Pi 5 Pro）设计的高性能、低延迟视频推流引擎 适用于水下机器人等带宽受限场景进行视频流传输

该项目充分利用了 Rockchip 的硬件加速能力，实现了从摄像头采集、色彩空间转换、H.264 编码到网络传输的全链路优化。

## 🚀 核心特性

*   **V4L2 采集**：直接操作底层 Video4Linux2 接口，获取原始 YUYV 图像数据。
*   **硬件加速转换 (RGA)**：使用 Rockchip **RGA (2D Graphic Acceleration)** 进行格式转换（YUYV -> NV12）和缩放。
*   **板载AI算力 (NPU)**：集成 Rockchip **NPU (Network Process Unite)**，利用6 Tops算力进行硬件加速。
*   **硬件编码 (MPP)**：集成 Rockchip **MPP (Media Process Platform)**，实现 H.264 硬件编码。
*   **UDP 低延迟传输**：通过原始 UDP Socket 发送 H.264 码流，极大减少传输开销。

## 🛠️ 硬件与环境要求

*   **硬件平台**：Orange Pi 5 / 5 Pro / 5 Plus (基于 RK3588/RK3588S)
*   **操作系统**：Ubuntu 20.04 / 22.04 (Rockchip BSP)
*   **依赖库**：
    *   `libv4l-dev`
    *   `librga-dev` (Rockchip RGA)
    *   `librockchip-mpp-dev` (Rockchip MPP)
    *   `opencv` (可选，目前仅用于测试各模块效果<!--  -->)

## 📂 项目结构

```text
.
├── CMakeLists.txt                  # CMake文件
├── inc/                            # 头文件
│   ├── dma_utils.h                 # DMA Buffer 管理
│   ├── mpp_encoder.h               # MPP 编码封装类
│   └── v4l2_utils.h                # V4L2 操作封装
├── src/                            # 源代码
│   ├── main.cpp                    # 主程序入口 (采集->RGA->MPP->UDP)
│   ├── postprocess.cc              # 官方：后处理程序
│   ├── yolo_detector.cpp           # 目标检测封装类
│   └── mpp_encoder.cpp             # MPP 编码实现
├── utils/                          # 辅助文件
│   ├── dma_utils.cpp               # 实现 dma-heap 内存分配
│   └── v4l2_utils.c                # V4L2 采集实现
├── python_demo/                    # python demo
│   ├── yolov5_rknn.py              # rknn的python demo
│   └── yolov5_ROS.py               # rknn集成ROS demo
├── model/                          # 模型文件
│   ├── yolov5s.rknn                # rknn转化模型文件
│   └── coco_80_labels_list.txt     # 模型标签文件
├── .gitignore                      # git辅助文件
└── README.md                       # 说明文档
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

ffplay -f h264 udp://0.0.0.0:8888     -fflags nobuffer     -flags low_delay     -framedrop     -strict experimental     -probesize 32     -analyzeduration 0     -sync ext

```

### 2. 发送端配置 (Orange Pi)

修改 `src/main.cpp` 中的目标 IP，将其指向你的 PC：

```cpp
#define DEST_IP   "192.168.13.xxx" // <--- 修改为你 PC 的 IP 地址
#define DEST_PORT 8888            // <--- 确保没有被防火墙拦截
```

### 3. 开始推流

在板子上执行编译好的程序：

```bash
cd build
sudo ./bricsbot_vision
```

如果一切正常，你应该能在 PC 上看到来自摄像头的实时画面，且延迟极低。

## 📝 关键参数说明

在 `src/main.cpp` 中可以调整以下宏定义：

*   `DST_WIDTH` / `DST_HEIGHT`: RGA 输出和 MPP 编码的分辨率（默认 640x640）。
*   `UDP_MTU`: UDP 分包大小（默认 1024），建议小于 MTU 1500。

在 `src/mpp_encoder.cpp` 中可以调整编码参数：

*   `bps` (Bitrate): 目标码率，当前设置为 2Mbps ~ 3Mbps。
*   `rc:mode`: 码率控制模式 (CBR 固定码率)。

## 💡 技术亮点

本项目在嵌入式视觉领域有以下几个值得关注的技术创新点：

### 1. 零拷贝 DMA 全链路架构

这是本项目最核心的性能优化手段。整个数据流（摄像头 → RGA → MPP 编码器 / NPU 推理）全程通过 **DMA 文件描述符（fd）** 来传递内存引用，而非拷贝数据本身。

```
V4L2 (DMA-buf fd) → RGA → MPP Encoder (DMA-buf fd)
                       ↘
                        NPU Inference (DMA-buf fd)
```

- `dma_utils.cpp` 封装了 `/dev/dma_heap` 的内存分配，所有模块共享同一块物理内存地址。
- 避免了 CPU 参与的 `memcpy`，极大降低了 CPU 占用率和流水线延迟。

---

### 2. 单帧双路 RGA 并行输出

摄像头采集到一帧 YUYV422 图像后，`main.cpp` 利用 RGA 同时向两条路径输出，完全由硬件并行完成，CPU 无需介入：

| 路径 | 目标格式 | 目标分辨率 | 用途 |
|------|----------|-----------|------|
| 编码路径 | NV12 (YUV420 Semi-Planar) | 640×640 | MPP H.264 硬件编码 |
| 推理路径 | RGB888 | 640×640 | RKNN NPU 目标检测推理 |

这种设计避免了两次独立的格式转换，同时也证明了 RGA 可以作为一个高效的"数据分发器"。

---

### 3. 三级 DMA Heap 自动降级策略

`dma_utils.cpp` 在分配 DMA 内存时实现了智能降级逻辑，确保跨板卡兼容性：

```
优先级 1: /dev/dma_heap/linux,cma   （物理连续内存，最优，RGA/MPP 首选）
    ↓ 失败
优先级 2: /dev/dma_heap/system-uncached  （非缓存系统内存，跳过 CPU Cache）
    ↓ 失败
优先级 3: /dev/dma_heap/system      （通用系统内存，最后保底）
```

同时提供显式的缓存同步接口：
- `dma_sync_cpu()`：CPU 读取前使 Cache 失效（Invalidate），确保读到最新硬件写入的数据。
- `dma_sync_device()`：硬件读取前刷新 Cache（Flush），确保硬件读到 CPU 写入的最新数据。

---

### 4. 全硬件加速四级流水线

本项目将 RK3588 片上四个独立的硬件加速单元串联为一条完整的流水线：

```
[CPU]    V4L2 采集 (ring buffer, mmap)
  ↓
[RGA]    格式转换 + 缩放 (YUYV422 800×600 → NV12/RGB888 640×640)
  ↓
[MPP]    H.264 CBR 硬件编码 (3 Mbps, GOP=30)
  ↓
[UDP]    原始码流网络发送 (MTU 1024 分包)

同时:
[NPU]    YOLOv5s INT8 推理 (RKNN 量化模型, 理论峰值 6 TOPS)
  ↓
[RGA]    检测框硬件绘制 (imrectangle, 可选 OpenCV)
```

各加速单元各司其职，CPU 仅负责任务调度和 UDP 发包，处理器占用率可低至 10-20%。

---

### 5. RKNN INT8 量化推理

模型以 RKNN 格式（`.rknn`）存储，在导出时已完成 **INT8 量化**：

- 相较于 FP32，推理速度和内存效率均有显著提升（具体收益因模型结构和输入数据而异）。
- 精度损失极小（以 YOLOv5s 为例，COCO mAP 通常约下降 2-3%，实际结果因量化配置而有所不同）。
- 与 Rockchip NPU（理论峰值算力 6 TOPS）深度配合，实现实时 30 fps 检测。
- `yolo_detector.cpp` 封装了 RKNN API 的完整生命周期（初始化 → 推理 → 释放）。

---

### 6. RGA 硬件绘制检测框

目标检测后的边界框渲染通过预处理宏在两种实现间切换：

```cpp
#define _USE_OPENCV_DRAW 0  // 0: 使用 RGA imrectangle() 硬件绘制
                            // 1: 使用 OpenCV cv::rectangle() 软件绘制
```

使用 `imrectangle()` 时，矩形绘制由 RGA 硬件完成，CPU 无需计算任何像素，适合高帧率场景。

---

### 7. V4L2 环形缓冲区防帧丢失

`v4l2_utils.c` 为摄像头采集配置了 **4 帧环形缓冲区（ring buffer）**，通过 `mmap` 将内核缓冲区直接映射到用户空间：

- 后端（摄像头 DMA）持续填充缓冲区，前端（处理线程）异步消费。
- 有效吸收编码/推理带来的处理抖动，避免在高负载时丢帧。
- 支持最高 30 fps（约 33 ms/帧间隔）的稳定采集。

---

### 8. CBR 固定码率控制（适配带宽受限链路）

MPP 编码器采用 **CBR（Constant Bit Rate，固定码率）** 模式，当前配置为：

| 参数 | 值 | 说明 |
|------|-----|------|
| 目标码率 | 3 Mbps | 适合水下 ROV 等受限信道 |
| 帧率 | 30 fps | 与采集同步 |
| GOP | 30 帧 | 每秒一个 I 帧，保证丢包后快速恢复 |
| 编码格式 | H.264 Baseline | 兼容性最广 |

固定码率可预测的带宽占用，是水下机器人通信链路的关键需求。

---

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