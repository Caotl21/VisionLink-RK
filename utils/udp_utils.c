#include "../inc/udp_utils.h"
#include <stdio.h>

/** 
 * @brief   初始化UDP上下文，创建UDP socket并设置目标地址
 * @param   ctx     UDP上下文结构体指针
 * @param   dest_ip 目标IP地址字符串
 * @param   port    目标端口号
 * @return  0 成功，-1 失败
**/
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

/** 
 * @brief   通过UDP发送数据，自动进行分片
 * @param   ctx     UDP上下文结构体指针
 * @param   data    待发送数据指针
 * @param   len     待发送数据长度
 * @return  无返回值
 * @remark  该函数会将输入数据分成多个小块（每块不超过UDP_MTU），并通过UDP socket发送到目标地址。调用者无需关心分片细节，直接调用此函数即可发送任意大小的数据。
 *          注意：UDP协议不保证数据包的可靠送达和顺序，因此在网络状况不佳时可能会丢包或乱序。调用者需要根据实际需求考虑是否需要在应用层实现重传机制或使用更可靠的传输协议。
 *          该函数适用于发送编码后的H.264帧数据，尤其是I帧等较大的数据块，可以有效避免IP层的分片，提高传输效率和成功率。
**/
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