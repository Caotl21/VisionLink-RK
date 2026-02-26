#include "../inc/udp_utils.h"
#include <stdio.h>

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