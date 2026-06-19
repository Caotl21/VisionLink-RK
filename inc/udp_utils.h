// socket相关
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#ifdef __cplusplus
extern "C"{
#endif

#define UDP_MTU  1024  // UDP分片大小，通常小于1500字节以避免IP层分片

typedef struct UdpContext{
    int socket_fd;
    struct sockaddr_in dest_addr;
} UdpContext;

int udp_init(UdpContext *ctx, const char *dest_ip, int port);
void udp_send(UdpContext *ctx, void *data, size_t len);

#ifdef __cplusplus
}
#endif