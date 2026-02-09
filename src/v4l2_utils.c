#include "../inc/v4l2_utils.h"


/**
 *   @brief   查询视频设备能力，列出支持的像素格式、分辨率和帧率
 *   @param   fd  视频设备文件描述符
 *   @return  0 成功，-1 失败
**/
int v4l2_capability_query(int fd) {
    // 查询视频设备能力
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    // 发送VIDIOC_QUERYCAP命令查询设备能力
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }

    struct v4l2_fmtdesc fmtdesc = {0};//定义支持的像素格式结构体
    struct v4l2_frmsizeenum frmsize = {0};//定义支持的分辨率结构体
    struct v4l2_frmivalenum frmival = {0};//定义支持的帧率结构体

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    static cam_fmt cam_fmts[2];

    while(!ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))// 查询支持的像素格式
    {
        // 安全拷贝并手动补 '\0'
        strncpy(cam_fmts[fmtdesc.index].description,
                (const char*)fmtdesc.description,
                sizeof(cam_fmts[fmtdesc.index].description) - 1);
        cam_fmts[fmtdesc.index].description[sizeof(cam_fmts[fmtdesc.index].description) - 1] = '\0';
        cam_fmts[fmtdesc.index].pixelformat = fmtdesc.pixelformat;
        fmtdesc.index++;
    }

    frmsize.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    frmival.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    for(int i=0;i<fmtdesc.index;i++) // 遍历支持的像素格式
    {
        //printf("支持的像素格式 %d: %s\n", i, cam_fmts[i].description);
        frmsize.index = 0;
        frmsize.pixel_format = cam_fmts[i].pixelformat;
        frmival.pixel_format = cam_fmts[i].pixelformat;

        // 遍历出所有摄像头支持的视频采集分辨率
        while(ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0){
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf("  支持的分辨率 %d: %ux%u\n", frmsize.index,
                       frmsize.discrete.width, frmsize.discrete.height);
            }
            frmsize.index++;
            frmival.index = 0;
            frmival.width = frmsize.discrete.width;
            frmival.height = frmsize.discrete.height;
            // 遍历出所有摄像头支持的帧率
            while(ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0)
            {
                if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                    printf("    支持的帧率 %d: %u/%u fps\n", frmival.index,
                           frmival.discrete.numerator, frmival.discrete.denominator);
                }
                frmival.index++;
            }
        }
    }
}

/** 
 * @brief   初始化视频设备
 * @param   ctx     V4L2 上下文结构体
 * @param   device  视频设备路径
 * @return  0 成功，-1 失败
**/
int v4l2_init(V4L2Context *ctx, const char *device) {
    
    // 打开视频设备
    ctx->fd = open(device, O_RDWR);
    if (ctx->fd < 0) {
        perror("Opening video device");
        return -1;
    }

    // 设置采集参数
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 设置为视频捕获类型
    fmt.fmt.pix.width = SRC_WIDTH;               // 设置宽度
    fmt.fmt.pix.height = SRC_HEIGHT;              // 设置高度
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; // 设置像素格式
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if(ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Setting Pixel Format");
        return -1;
    }

    // 设置帧率参数
    struct v4l2_streamparm streamparm = {0};
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(ctx->fd, VIDIOC_G_PARM, &streamparm); // 获取当前帧率参数
    if (streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
        streamparm.parm.capture.timeperframe.numerator = 1;   // 分子
        streamparm.parm.capture.timeperframe.denominator = FPS; // 分母，设置为30fps
        if(ioctl(ctx->fd, VIDIOC_S_PARM, &streamparm) < 0) { // 设置帧率参数
            perror("Setting Frame Rate");
            return -1;
        }
    }

    printf("设置视频格式成功: width=%d, height=%d\n", fmt.fmt.pix.width, fmt.fmt.pix.height);

    // 申请Buffer
    struct v4l2_requestbuffers req = {0};
    req.count = 1; // 请求1个缓冲区
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP; // 内存映射方式
    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("Requesting Buffer");
        return -1;
    }
    printf("成功请求 %d 个缓冲区\n", req.count);

    // 映射Buffer到用户空间
    for(int i=0;i<req.count;i++){
        struct v4l2_buffer buffer = {0};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;
        if(ioctl(ctx->fd, VIDIOC_QUERYBUF, &buffer) == -1)
        {
            perror("Querying Buffer");
            return -1;
        }
        ctx->mptr[i] = (unsigned char *)mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, buffer.m.offset);
        ctx->mlength[i] = buffer.length;
        // 使用完毕 入队
        if(ioctl(ctx->fd, VIDIOC_QBUF, &buffer) == -1)
        {
            perror("Queueing Buffer");
            return -1;
        }
        printf("缓冲区 %d 映射到地址 %p，长度 %u 字节\n", i, ctx->mptr[i], ctx->mlength[i]);
    }

    // 开始采集视频
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Starting Capture");
        return -1;
    }

    printf("视频采集已启动...\n");

    return 0;
}

/** 
 * @brief   获取一帧视频数据
 * @param   ctx V4L2 上下文结构体
 * @return  成功返回指向帧数据的指针，失败返回 NULL
 * @remark  调用该函数会从视频设备出队一个缓冲区，并返回指向该缓冲区数据的指针。
            调用者在处理完数据后需要调用 v4l2_release_frame() 将缓冲区重新入队。
**/
unsigned char* v4l2_get_frame(V4L2Context *ctx){
    memset(&ctx->buffer, 0, sizeof(struct v4l2_buffer));
    ctx->buffer.memory = V4L2_MEMORY_MMAP;
    ctx->buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 出队
    if(ioctl(ctx->fd, VIDIOC_DQBUF, &ctx->buffer) < 0) {
        perror("Dequeue Buffer");
        return NULL;
    }
    return ctx->mptr[ctx->buffer.index];
}

/** 
 * @brief   释放一帧视频数据
 * @param   ctx V4L2 上下文结构体
 * @remark  调用该函数会将之前获取的帧缓冲区重新入队，以便后续继续使用。
**/
void v4l2_release_frame(V4L2Context *ctx){
    // 入队
    if(ioctl(ctx->fd, VIDIOC_QBUF, &ctx->buffer) < 0) {
        perror("Queue Buffer");
    }
}

/** 
 * @brief   关闭视频设备
 * @param   ctx V4L2 上下文结构体
 * @remark  调用该函数会停止视频采集，释放映射的缓冲区，并关闭设备文件描述符。
**/
void v4l2_deinit(V4L2Context *ctx){
    // 停止视频采集
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Stopping Capture");
    }
    printf("视频采集已停止\n");

    // 释放映射的缓冲区
    for(int i=0;i<4;i++){
        munmap(ctx->mptr[i], ctx->mlength[i]);
    }

    close(ctx->fd);
}