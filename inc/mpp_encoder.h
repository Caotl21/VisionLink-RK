#ifndef MPP_ENCODER_H
#define MPP_ENCODER_H

#include <stdint.h>
#include <stdio.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>

class MppEncoder {
public:
    MppEncoder();
    ~MppEncoder();

    // 初始化编码器 (宽, 高, fps)
    int init(int width, int height, int fps);

    // 编码一帧
    // 输入: dma_fd (RGA转换好的 NV12 数据的 FD)
    // 输出: out_data (H264码流指针), out_len (码流长度)
    // 返回: 0 成功
    int encode(int dma_fd, void **out_data, size_t *out_len);

    // 释放资源
    void deinit();

private:
    int width;
    int height;
    
    // MPP 上下文
    MppCtx ctx;
    MppApi *mpi;

    MppBufferGroup buf_grp;

    // 拼包缓冲区临时变量
    char *packet_buf;
    size_t packet_buf_size;
};

#endif // MPP_ENCODER_H