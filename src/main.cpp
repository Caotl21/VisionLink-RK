#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <thread>
#include <memory>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/fb.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>

// opencv相关
#include <opencv2/opencv.hpp>

// Rockchip SDK 相关
#include "im2d.h"
#include "rga.h"
#include "dma_utils.h"
#include "mpp_encoder.h"
#include "v4l2_utils.h"
#include "yolo_detector.h"

// RTSP库
#include "xop/RtspServer.h"
#include "xop/H264Source.h"

// 统计用时
#include "count_utils.h"

// UDP裸发辅助函数
#include "udp_utils.h"


#define VIDEO_DEVICE        "/dev/video0"       // 摄像头设备路径
#define DST_WIDTH           640                 // RGA 输出宽度
#define DST_HEIGHT          640                 // RGA 输出高度
#define DEST_IP             "192.168.13.10"     // 目标IP地址
#define DEST_PORT           8888                // 目标端口号
#define _Capability_Query   0                   // 定义该宏以启用设备能力查询功能
#define _USE_OPENCV_DRAW    1                   // 定义该宏以启用OPENCV绘制检测框
#define _USE_PURE_UDP       0                   // 定义该宏以启用裸UDP分发


int main(int argc, char *argv[]){

    int ret;

    StageStat s_get{"v4l2_get"};
    StageStat s_rga1{"rga_yuyv2rgb"};
    StageStat s_npu{"npu_infer"};
    StageStat s_opencv{"draw_box"};
    StageStat s_rga2{"rga_rgb2nv12"};
    StageStat s_enc{"mpp_encode"};
    StageStat s_push{"rtsp_push"};
    StageStat s_total{"total"};
    int stat_frames = 0;

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

#if _USE_PURE_UDP
    UdpContext udp_ctx;
    if(udp_init(&udp_ctx, DEST_IP, DEST_PORT) < 0){
        printf("Failed to initialize UDP\n");
        return -1;
    }
#else
    // 创建事件循环
    std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
    // 2. 创建 RTSP Server，监听 8554 端口
    std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
    if (!server->Start("0.0.0.0", 8554)) {
        printf("RTSP Server start failed!\n");
        return -1;
    }
    // 3. 创建一个叫 "live" 的流媒体会话 (推流地址就是 rtsp://ip:8554/live)
    xop::MediaSession* session = xop::MediaSession::CreateNew("live");
    // 4. 添加 H.264 视频源通道
    session->AddSource(xop::channel_0, xop::H264Source::CreateNew());
    // session->AddNotifyConnectedCallback([] (xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port){
    //     printf("有 PC 客户端连接进来了: %s\n", peer_ip.c_str());
    // });
    xop::MediaSessionId session_id = server->AddSession(session);

    // 5. 启动一个后台线程让 RTSP 服务器运行 (处理网络请求，不卡主线程)
    std::thread rtsp_thread([event_loop]() {
        event_loop->Loop();
    });
    rtsp_thread.detach(); // 分离线程，让它自己在后台跑

    printf("RTSP Server is running at rtsp://192.168.13.11:8554/live\n");

#endif

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
    rga_buffer_t src_img, infer_img, dst_img;
    memset(&src_img, 0, sizeof(src_img));
    memset(&infer_img, 0, sizeof(infer_img));
    memset(&dst_img, 0, sizeof(dst_img));
    dst_img = wrapbuffer_fd(mpp_buf.fd, DST_WIDTH, DST_HEIGHT, RK_FORMAT_YCbCr_420_SP);
    infer_img = wrapbuffer_fd(npu_buf.fd, DST_WIDTH, DST_HEIGHT, RK_FORMAT_RGB_888);

    MppEncoder encoder;
    // 初始化摄像头分辨率 30fps
    if(encoder.init(DST_HEIGHT,DST_WIDTH,30) < 0){
        printf("MPP Failed to initialize encoder\n");
        return -1;
    }

    RKNNDetector detector;
    if(detector.init("../model/yolov5s-640-640.rknn") < 0){
        printf("Failed to initialize RKNNDetector\n");
        return -1;
    }

    // 调试用：Mat对象指向npu_buf虚拟地址
    cv::Mat orig_img(DST_HEIGHT, DST_WIDTH, CV_8UC3, npu_buf.vaddr);
    cv::Mat show_img(DST_HEIGHT, DST_WIDTH, CV_8UC3);

    while(1)
    {
        static int frame_cnt = 0;

        int64_t t0 = now_us();

        int64_t tg0 = now_us();
        // 出队
        unsigned char *src_ptr = v4l2_get_frame(&v4l2_ctx);

        int64_t tg1 = now_us();
        s_get.add(tg1 - tg0);

        if(!src_ptr){
            printf("Failed to get frame from V4L2\n");
            continue;
        }

        int64_t tr10 = now_us();
        // RGA 进行格式转换和缩放，输入 src_ptr (YUYV422)，输出 infer_img (RGB888)
        src_img = wrapbuffer_virtualaddr(src_ptr, SRC_WIDTH, SRC_HEIGHT, RK_FORMAT_YUYV_422);
        IM_STATUS status = imresize(src_img, infer_img);
        int64_t tr11 = now_us();
        s_rga1.add(tr11 - tr10);
        
        if (status != IM_STATUS_SUCCESS) {
            printf("RGA Error: %s\n", imStrError(status));
            v4l2_release_frame(&v4l2_ctx);
            continue;
        }

        // 执行推理
        std::vector<DetectResult> results;
        int64_t tn0 = now_us();
        int ret = detector.inference((unsigned char*)npu_buf.vaddr, results);
        int64_t tn1 = now_us();
        s_npu.add(tn1 - tn0);

#if _USE_OPENCV_DRAW
        // 使用OpenCV将推理结果绘制到原图上
        dma_sync_cpu(npu_buf.fd);

        if(ret==0){
            if(!results.empty()){
                int64_t td0 = now_us();
                printf("=============================================================\n");
                for(const auto&res:results){
                    //printf("OpenCV: Detected: ID=%d, Name=%s, Confidence=%.2f, Box=(%d, %d, %d, %d)\n",
                    //       res.id, res.name.c_str(), res.confidence,
                    //       res.box.left, res.box.top, res.box.right, res.box.bottom);
                    cv::rectangle(orig_img, cv::Point(res.box.left, res.box.top), cv::Point(res.box.right, res.box.bottom), cv::Scalar(0, 255, 0), 3);
                    cv::putText(orig_img, res.name, cv::Point(res.box.left, res.box.top + 12), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255));
                }
                int64_t td1 = now_us();
                s_opencv.add(td1 - td0);
            }
        }else{
            printf("RKNN inference failed with error code: %d\n", ret);
        }

        dma_sync_device(npu_buf.fd);
#else
        // 使用RGA将推理结果绘制到原图上
        if(ret==0 && !results.empty()){
            printf("=============================================================\n");
            
            uint32_t color = 0xFF00FF00; // 绿色（若颜色异常再换通道顺序）
            int thickness = 2;

            for(const auto&res:results){
                printf("RGA: Detected: ID=%d, Name=%s, Confidence=%.2f, Box=(%d, %d, %d, %d)\n",
                       res.id, res.name.c_str(), res.confidence,
                       res.box.left, res.box.top, res.box.right, res.box.bottom);
                im_rect rect;
                rect.x = res.box.left;
                rect.y = res.box.top;
                rect.width = res.box.right - res.box.left;
                rect.height = res.box.bottom - res.box.top;
                // 绘制矩形框
                status = imrectangle(infer_img, rect, color, thickness); //同步
                if (status != IM_STATUS_SUCCESS) {
                    printf("RGA rectangle Error: %s\n", imStrError(status));
                }

            }
        }
#endif 
        int64_t tr20 = now_us();
        status = imresize(infer_img, dst_img);
        int64_t tr21 = now_us();
        s_rga2.add(tr21 - tr20);
        
        if (status != IM_STATUS_SUCCESS) {
            printf("RGA Error: %s\n", imStrError(status));
            v4l2_release_frame(&v4l2_ctx);
            continue;
        }

        // 编码 NV12 数据，得到 H264 码流
        void *h264_data = NULL;
        size_t h264_len = 0;

        int64_t te0 = now_us();
        int enc_ok = encoder.encode(mpp_buf.fd, &h264_data, &h264_len) == 0;
        int64_t te1 = now_us();
        s_enc.add(te1 - te0);

        if(enc_ok){
#if _USE_PURE_UDP
            if(h264_len > 0){
                // 网络发送代码
                printf("Frame %d encoded: %zu bytes\n", frame_cnt++, h264_len);
                udp_send(&udp_ctx, h264_data, h264_len);
            }
#else
            if (h264_len > 0) {
                xop::AVFrame videoFrame = {0};
                videoFrame.type = 0; // 0 代表视频，1 代表音频
                videoFrame.size = h264_len;
                videoFrame.timestamp = xop::H264Source::GetTimestamp(); // 自动打时间戳
                
                // 拷贝数据给 RTSP 库 (它会自动进行 RTP 分包和发送)
                videoFrame.buffer.reset(new uint8_t[videoFrame.size]);
                memcpy(videoFrame.buffer.get(), h264_data, videoFrame.size);
                
                // 把这一帧推送到 "live" 这个通道
                int64_t tp0 = now_us();
                server->PushFrame(session_id, xop::channel_0, videoFrame);
                int64_t tp1 = now_us();
                s_push.add(tp1 - tp0);
            }
#endif
        } else{
            printf("MPP Failed to encode frame\n");
        }

        // 入队
        v4l2_release_frame(&v4l2_ctx);

        int64_t t1 = now_us();
        s_total.add(t1 - t0);

        stat_frames++;
        if (stat_frames >= 60) {
            printf("--------------------------------------------------\n");
            auto pr = [](const StageStat& s){
                double avg = s.cnt ? (double)s.sum_us / s.cnt / 1000.0 : 0.0;
                double mx  = (double)s.max_us / 1000.0;
                printf("[LAT] %-12s avg=%7.2f ms max=%7.2f ms\n", s.name, avg, mx);
            };
            pr(s_get); pr(s_rga1); pr(s_npu); pr(s_opencv); pr(s_rga2); pr(s_enc); pr(s_push); pr(s_total);
            printf("--------------------------------------------------\n");

            s_get.reset(); s_rga1.reset(); s_npu.reset(); s_opencv.reset(); s_rga2.reset();
            s_enc.reset(); s_push.reset(); s_total.reset();
            stat_frames = 0;
        }

    }

    // 释放 RGA 目标缓冲区
    free_dma_buffer(&mpp_buf);

    v4l2_deinit(&v4l2_ctx);
    //close(udp_ctx.socket_fd);
    return 0;

}

