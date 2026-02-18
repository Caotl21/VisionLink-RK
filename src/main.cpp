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
#include "yolo_detector.h"

#define VIDEO_DEVICE        "/dev/video0"       // 摄像头设备路径
#define DST_WIDTH           640                 // RGA 输出宽度
#define DST_HEIGHT          640                 // RGA 输出高度
#define DEST_IP             "192.168.13.10"     // 目标IP地址
#define DEST_PORT           8888                // 目标端口号
#define UDP_MTU             1024                // UDP分片大小，通常小于1500字节以避免IP层分片
#define _Capability_Query   0                   // 定义该宏以启用设备能力查询功能

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

    int sock_buf_size = 32 * 1024; // 32KB，足够放一个 I 帧，但放不下更多
    if (setsockopt(ctx->socket_fd, SOL_SOCKET, SO_SNDBUF, &sock_buf_size, sizeof(sock_buf_size)) < 0) {
        perror("设置发送缓冲区失败");
    }

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

#if _Capability_Query
    // 查询视频设备能力，列出支持的像素格式、分辨率和帧率
    v4l2_capability_query(v4l2_ctx.fd);
#endif

    UdpContext udp_ctx;
    if(udp_init(&udp_ctx, DEST_IP, DEST_PORT) < 0){
        printf("Failed to initialize UDP\n");
        return -1;
    }

    // 申请一块DMA内存给MPP用 MPP需要NV12 大小 W*H*1.5
    size_t mpp_size = DST_HEIGHT * DST_WIDTH * 3 / 2;
    struct DmaBuffer mpp_buf;
    if(alloc_dma_buffer(mpp_size, &mpp_buf) < 0) {
        free_dma_buffer(&mpp_buf);
        perror("MPP buffer alloc_dma_buffer failed");
        return -1;
    }

    // 申请一块内存给NPU用 NPU需要RGB888 大小 W*H*3
    size_t npu_size = DST_HEIGHT * DST_WIDTH * 3;
    struct DmaBuffer npu_buf;
    if (alloc_dma_buffer(npu_size, &npu_buf)<0) {
        perror("NPU buffer alloc_dma_buffer failed");
        free_dma_buffer(&mpp_buf);
        return -1;
    }

    // 定义 RGA 相关变量
    rga_buffer_t src_img, dst_img;
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));
    //dst_img = wrapbuffer_fd(mpp_buf.fd, DST_WIDTH, DST_HEIGHT, RK_FORMAT_YCbCr_420_SP);
    dst_img = wrapbuffer_fd(npu_buf.fd, DST_WIDTH, DST_HEIGHT, RK_FORMAT_RGB_888);

    MppEncoder encoder;
    // 初始化摄像头分辨率 30fps
    if(encoder.init(DST_HEIGHT,DST_WIDTH,30) < 0){
        printf("MPP Failed to initialize encoder\n");
        return -1;
    }

    RKNNDetector detector;
    if(detector.init("../model/yolov5s.rknn") < 0){
        printf("Failed to initialize RKNNDetector\n");
        return -1;
    }

    // 调试用：Mat对象指向npu_buf虚拟地址
    cv::Mat orig_img(DST_HEIGHT, DST_WIDTH, CV_8UC3, npu_buf.vaddr);
    cv::Mat show_img(DST_HEIGHT, DST_WIDTH, CV_8UC3);

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

        // 执行推理
        std::vector<DetectResult> results;
        int ret = detector.inference((unsigned char*)npu_buf.vaddr, results);
        if(ret==0){
            if(!results.empty()){
                printf("=============================================================\n");
                for(const auto&res:results){
                    printf("Detected: ID=%d, Name=%s, Confidence=%.2f, Box=(%d, %d, %d, %d)\n",
                           res.id, res.name.c_str(), res.confidence,
                           res.box.left, res.box.top, res.box.right, res.box.bottom);
                    cv::rectangle(orig_img, cv::Point(res.box.left, res.box.top), cv::Point(res.box.right, res.box.bottom), cv::Scalar(256, 0, 0, 256), 3);
                    cv::putText(orig_img, res.name, cv::Point(res.box.left, res.box.top + 12), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255));
                }
            }
        }else{
            printf("RKNN inference failed with error code: %d\n", ret);
        }

        // 显示（RGB -> BGR）
        cv::cvtColor(orig_img, show_img, cv::COLOR_RGB2BGR);
        cv::imshow("YOLOv5-Preview", show_img);
        if(cv::waitKey(1) == 27) { // 按 ESC 退出
            break;
        }

        // 编码 NV12 数据，得到 H264 码流
        //void *h264_data = NULL;
        //size_t h264_len = 0;

        /*if(encoder.encode(mpp_buf.fd, &h264_data, &h264_len) == 0){
            if(h264_len > 0){
                // 网络发送代码
                printf("Frame %d encoded: %zu bytes\n", frame_cnt++, h264_len);
                udp_send(&udp_ctx, h264_data, h264_len);
            }
        } else{
            printf("MPP Failed to encode frame\n");
        }*/

        // 入队
        v4l2_release_frame(&v4l2_ctx);

    }

    // 释放 RGA 目标缓冲区
    free_dma_buffer(&mpp_buf);

    v4l2_deinit(&v4l2_ctx);
    close(udp_ctx.socket_fd);
    return 0;

}

