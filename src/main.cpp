#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

// socket相关
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// opencv相关
#include <opencv2/opencv.hpp>

// Rockchip SDK 相关
#include "im2d.h"
#include "rga.h"
#include "dma_utils.h"
#include "mpp_encoder.h"
#include "v4l2_utils.h"

#define VIDEO_DEVICE    "/dev/video0"
#define DST_WIDTH       640
#define DST_HEIGHT      640
#define DEST_IP         "192.168.1.115"
#define DEST_PORT       8888
#define UDP_MTU         1024

struct UdpContext{
    int socket_fd;
    struct sockaddr_in dest_addr;
};

int udp_init(UdpContext *ctx, const char *dest_ip, int port){
    ctx->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->socket_fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    memset(&ctx->dest_addr, 0, sizeof(ctx->dest_addr));
    ctx->dest_addr.sin_family = AF_INET;
    ctx->dest_addr.sin_port = htons(port);
    ctx->dest_addr.sin_addr.s_addr = inet_addr(dest_ip);

    return 0;
}

void udp_send(UdpContext *ctx, void *data, size_t len){
     uint8_t *send_ptr = (uint8_t*)data;
     size_t remain = len;

     while(remain > 0){
         size_t chunk = (remain > UDP_MTU) ? UDP_MTU : remain;
         sendto(ctx->socket_fd, send_ptr, chunk, 0 , (struct sockaddr *)&ctx->dest_addr, sizeof(ctx->dest_addr));
         send_ptr += chunk;
         remain -= chunk;
     }
}


int main(int argc, char *argv[]){

    int ret;

    // 初始化V4L2，打开摄像头设备，设置分辨率和帧率，申请缓冲区并映射到用户空间
    V4L2Context v4l2_ctx;
    if(v4l2_init(&v4l2_ctx, VIDEO_DEVICE) < 0){
        printf("Failed to initialize V4L2\n");
        return -1;
    }

    UdpContext udp_ctx;
    if(udp_init(&udp_ctx, DEST_IP, DEST_PORT) < 0){
        printf("Failed to initialize UDP\n");
        return -1;
    }

    // 申请一块DMA内存给MPP用 MPP需要NV12 大小 W*H*1.5
    size_t mpp_size = DST_HEIGHT * DST_WIDTH * 3 / 2;
    struct DmaBuffer mpp_buf;
    if(alloc_dma_buffer(mpp_size, &mpp_buf) < 0) {
        perror("MPP buffer alloc_dma_buffer failed");
        return -1;
    }

    // 定义 RGA 相关变量
    rga_buffer_t src_img, dst_img;
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));
    dst_img = wrapbuffer_fd(mpp_buf.fd, DST_WIDTH, DST_HEIGHT, RK_FORMAT_YCbCr_420_SP);

    MppEncoder encoder;
    // 初始化摄像头分辨率 30fps
    if(encoder.init(DST_HEIGHT,DST_WIDTH,30) < 0){
        printf("MPP Failed to initialize encoder\n");
        return -1;
    }

    while(1)
    {
        static int frame_cnt = 0;

        // 出队
        unsigned char *src_ptr = v4l2_get_frame(&v4l2_ctx);
        if(!src_ptr){
            printf("Failed to get frame from V4L2\n");
            continue;
        }
        
        // RGA 进行格式转换和缩放，输入 src_ptr (YUYV422)，输出 dst_img (NV12)
        src_img = wrapbuffer_virtualaddr(src_ptr, SRC_WIDTH, SRC_HEIGHT, RK_FORMAT_YUYV_422);
        IM_STATUS status = imresize(src_img, dst_img);
        
        if (status != IM_STATUS_SUCCESS) {
            printf("RGA Error: %s\n", imStrError(status));
            v4l2_release_frame(&v4l2_ctx);
            continue;
        }

        // 编码 NV12 数据，得到 H264 码流
        void *h264_data = NULL;
        size_t h264_len = 0;

        if(encoder.encode(mpp_buf.fd, &h264_data, &h264_len) == 0){
            if(h264_len > 0){
                // 网络发送代码
                printf("Frame %d encoded: %zu bytes\n", frame_cnt++, h264_len);
                udp_send(&udp_ctx, h264_data, h264_len);
            }
        } else{
            printf("MPP Failed to encode frame\n");
        }

        // 入队
        v4l2_release_frame(&v4l2_ctx);

    }

    // 释放 RGA 目标缓冲区
    free_dma_buffer(&mpp_buf);

    v4l2_deinit(&v4l2_ctx);
    close(udp_ctx.socket_fd);
    return 0;

}

