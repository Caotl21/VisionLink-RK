#ifndef V4L2_UTILS_H
#define V4L2_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

#include <linux/videodev2.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUF_COUNT       4
#define SRC_WIDTH       640
#define SRC_HEIGHT      480
#define FPS             30

typedef struct V4L2Context {
    int fd;
    struct v4l2_buffer buffer; // 用于记录当前出队的 buffer 信息
    unsigned char *mptr[BUF_COUNT]; // 映射的虚拟地址
    unsigned int  mlength[BUF_COUNT];
} V4L2Context;

typedef struct camera_format{
    char description[32];  // 字符串描述
    unsigned int pixelformat;        // 像素格式
} cam_fmt;

int v4l2_capability_query(int fd);
int v4l2_init(V4L2Context *ctx, const char *device);
unsigned char* v4l2_get_frame(V4L2Context *ctx);
void v4l2_release_frame(V4L2Context *ctx);
void v4l2_deinit(V4L2Context *ctx);

#ifdef __cplusplus
}
#endif

#endif // V4L2_UTILS_H