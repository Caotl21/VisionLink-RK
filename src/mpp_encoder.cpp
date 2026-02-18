#include "mpp_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*   
    MppEncoder 类实现了基于 Rockchip MPP 的 H.264 视频编码功能。它提供了初始化编码器、编码帧数据和释放资源的接口。
    主要成员函数包括：
    - init(int width, int height, int fps)：初始化编码器，设置视频宽高和帧率。
    - encode(int dma_fd, void **out_data, size_t *out_len)：编码一帧数据，输入为 RGA 转换好的 NV12 数据的 DMA FD，输出为 H.264 码流指针和长度。
    - deinit()：释放编码器资源。

    内部使用 MPP API 创建编码上下文，配置编码参数，并管理输入帧和输出码流的缓冲区。调用者需要在使用 encode() 前先调用 init()，并在不需要编码时调用 deinit() 释放资源。
*/


MppEncoder::MppEncoder() : ctx(NULL), mpi(NULL), buf_grp(NULL) {
}

MppEncoder::~MppEncoder() {
    deinit();
}

/**  
 * @brief   初始化编码器
 * @param   width   视频宽度
 * @param   height  视频高度
 * @param   fps     视频帧率
 * @return  0 成功，-1 失败
 * @remark  该函数会创建 MPP 上下文，配置编码参数，并分配必要的缓冲区资源。调用者在使用 encode() 前必须调用此函数进行初始化。
 *          如果初始化失败，调用者应该检查返回值并进行相应的错误处理。
 *          成功初始化后，编码器将准备好接受帧数据进行编码。
 *          该函数内部会设置编码参数，如码率控制模式、目标码率、帧率等，以适应网络传输的需求。
**/
int MppEncoder::init(int width, int height, int fps){

    this->width = width;
    this->height = height;
    MPP_RET ret = MPP_OK;

    // 创建MPP上下文：编码器 H.264
    ret = mpp_create(&ctx, &mpi);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_create failed\n");
        return -1;
    }

    // 分配具体任务：编码 ENC , 编码格式 H.264
    ret = mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init failed\n");
        return -1;
    }

    // 分配一块外部DMA内存给MPP使用，MPP会从这个内存中读取输入帧数据
    ret = mpp_buffer_group_get(&buf_grp, MPP_BUFFER_TYPE_EXT_DMA, MPP_BUFFER_INTERNAL, NULL, __FUNCTION__);//EXT_DMA表示输入帧数据来自外部DMA内存，MPP_BUFFER_INTERNAL表示由MPP内部管理内存
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_group_get failed\n");
        return -1;
    }

    // 预分配拼包内存：2MB
    packet_buf_size = 2 * 1024 * 1024;
    packet_buf = (char*)malloc(packet_buf_size);
    if(!packet_buf){
        fprintf(stderr, "malloc packet_buf failed\n");
        return -1;
    }

    // 设置编码参数
    MppEncCfg cfg;
    mpp_enc_cfg_init(&cfg);

    mpp_enc_cfg_set_s32(cfg, "prep:width", width);
    mpp_enc_cfg_set_s32(cfg, "prep:height", height);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", width);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", height);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_YUV420SP); // RGA 输出的格式
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR); // 固定码率:适合网络传输
    
    // 目标码率3Mbps，最大码率4.5Mbps，最小码率1.5Mbps
    int bps = 3 * 1024 * 1024;
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", bps * 1.5);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", bps / 2);

    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", fps); // 每1秒一个关键帧

    // 应用配置
    ret = mpi->control(ctx, MPP_ENC_SET_CFG, cfg);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp control MPP_ENC_SET_CFG failed\n");
        return -1;
    }

    // 配置应用后释放cfg占据的系统内存
    mpp_enc_cfg_deinit(cfg);

    return 0;
}

/** 
 * @brief   编码一帧数据
 * @param   dma_fd   输入帧的 DMA FD (RGA转换好的 NV12 数据的 FD)
 * @param   out_data 输出参数，编码后的 H264 码流指针
 * @param   out_len  输出参数，编码后的码流长度
 * @return  0 成功，-1 失败
 * @remark  每调用一次 encode 就会编码一帧数据，编码后的数据通过 out_data 和 out_len 返回
 *          调用者需要负责释放 out_data 指向的内存。      
 */
int MppEncoder::encode(int dma_fd, void **out_data, size_t *out_len){

    MPP_RET ret = MPP_OK;

    // 创建一个 MppFrame 用于存放输入帧数据
    MppFrame frame = NULL;
    mpp_frame_init(&frame);
    mpp_frame_set_width(frame, width);
    mpp_frame_set_height(frame, height);
    mpp_frame_set_hor_stride(frame, width);
    mpp_frame_set_ver_stride(frame, height);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);

    // 设置FD 让MPP去读取数据
    MppBuffer buffer = NULL;
    MppBufferInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.type = MPP_BUFFER_TYPE_EXT_DMA;
    buffer_info.size = width * height * 3 / 2; // NV12 大小
    buffer_info.fd = dma_fd;
    buffer_info.index = 0; // 可选，表示第几个平面，NV12只有一个平面

    // 将外部DMA内存导入MPP，得到一个MPP可用的Buffer对象
    ret = mpp_buffer_import(&buffer, &buffer_info);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_buffer_import failed\n");
        mpp_frame_deinit(&frame);
        return -1;
    }
    // 将import的buffer内存句柄关联到设置好相关格式的frame上，供编码器读取
    mpp_frame_set_buffer(frame, buffer);

    // 送入编码器
    ret = mpi->encode_put_frame(ctx, frame);

    if(buffer){
        mpp_buffer_put(buffer);
    }

    // 防止内存泄漏，及时释放frame占用的系统内存
    mpp_frame_deinit(&frame);

    if (ret != MPP_OK) {
        fprintf(stderr, "encode_put_frame failed\n");
        return -1;
    }

    // [修复] 循环取包并拼接
    size_t total = 0;
    unsigned char *buf = NULL;

    while (1) {
        MppPacket pkt = NULL;
        ret = mpi->encode_get_packet(ctx, &pkt);
        if (ret != MPP_OK || pkt == NULL) break;

        // 获取包数据和长度
        size_t len = mpp_packet_get_length(pkt);
        void *ptr = mpp_packet_get_pos(pkt);

        // 安全检查：防止内存越界，确保有足够空间存放新数据
        if (total + len < packet_buf_size) {
            memcpy(packet_buf + total, ptr, len);
            total += len;
        }

        // 向MPP反馈释放包内存
        mpp_packet_deinit(&pkt);
    }
    
    if(total > 0){
        *out_data = packet_buf;
        *out_len = total;
    }

    return 0;

}


/*
    @Remark:    释放编码器资源这个函数会释放 MPP 上下文、缓冲区等资源。
                调用者在不需要编码时应该调用这个函数来清理资源。
*/
void MppEncoder::deinit(){
    if(ctx){
        mpp_destroy(ctx);
        ctx = NULL;
        mpi = NULL;
    }
    if(buf_grp){
        mpp_buffer_group_put(buf_grp);
        buf_grp = NULL;
    }
    if(packet_buf){
        free(packet_buf);
        packet_buf = NULL;
        packet_buf_size = 0;
    }
}