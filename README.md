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