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


int main(int argc, char *argv[]){

    int ret;

    // 初始化V4L2，打开摄像头设备，设置分辨率和帧率，申请缓冲区并映射到用户空间
    V4L2Context v4l2_ctx;
    if(v4l2_init(&v4l2_ctx, VIDEO_DEVICE) < 0){
        printf("Failed to initialize V4L2\n");
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

    // ==========================================================
    // [新增] 初始化 UDP Socket
    // ==========================================================
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8888); // 目标端口，PC端要监听这个
    // 【注意】这里必须填你 PC 的 IP 地址！请在 PC 终端输入 ipconfig 或 ifconfig 查看
    dest_addr.sin_addr.s_addr = inet_addr("192.168.1.115"); 

    // 设置 UDP 分包大小 (以太网 MTU 通常 1500，我们留点余量设 1400)
    const int UDP_PACKET_SIZE = 1024;

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
                uint8_t *send_ptr = (uint8_t*)h264_data;
                size_t remaining = h264_len;

                while (remaining > 0) {
                    // 计算当前包大小
                    size_t chunk_size = (remaining > UDP_PACKET_SIZE) ? UDP_PACKET_SIZE : remaining;

                    // 发送数据
                    sendto(sock_fd, send_ptr, chunk_size, 0, 
                           (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                    // 指针后移
                    send_ptr += chunk_size;
                    remaining -= chunk_size;
                }
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
    close(sock_fd);
    return 0;

}

