#include "dma_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>


/**
 *   @brief   分配 DMA 缓冲区并映射到用户空间
 *   @param   size  需要分配的内存大小
 *   @param   buf   输出参数，成功时包含 DMA 缓冲区信息
 *   @return  0 成功，-1 失败
**/
int alloc_dma_buffer(size_t size, struct DmaBuffer *buf) {
    // 优先尝试 CMA，其次是 Uncached System Heap
    const char *heap_paths[] = {
        "/dev/dma_heap/linux,cma",
        "/dev/dma_heap/system-uncached",
        "/dev/dma_heap/system"
    };

    int heap_fd = -1;
    for (const char *path : heap_paths) {
        heap_fd = open(path, O_RDWR);
        if (heap_fd >= 0) {
            printf("Using DMA Heap: %s\n", path); // 调试时可以打开
            break;
        }
    }

    if (heap_fd < 0) {
        perror("Failed to open any dma_heap");
        return -1;
    }

    struct dma_heap_allocation_data alloc_data = {0};
    alloc_data.len = size;
    alloc_data.fd_flags = O_CLOEXEC | O_RDWR;
    alloc_data.heap_flags = 0;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
        perror("DMA_HEAP_IOCTL_ALLOC failed");
        close(heap_fd);
        return -1;
    }
    close(heap_fd);

    buf->fd = alloc_data.fd;
    buf->size = size;

    // 映射内存
    buf->vaddr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd, 0);
    if (buf->vaddr == MAP_FAILED) {
        perror("mmap dma buffer failed");
        close(buf->fd);
        return -1;
    }

    return 0;
}

/**
 *   @brief   释放 DMA 缓冲区资源
 *   @param   buf 需要释放的 DMA 缓冲区信息
 *   @return  无
**/
void free_dma_buffer(struct DmaBuffer *buf) {
    if (buf->vaddr && buf->vaddr != MAP_FAILED) {
        munmap(buf->vaddr, buf->size);
    }
    if (buf->fd >= 0) {
        close(buf->fd);
    }
}

int dma_sync_cpu(int fd, bool start) {
    struct dma_buf_sync sync_args = {0};

    // 我们告诉内核：CPU 即将进行 READ 操作 (如果也要写，用 DMA_BUF_SYNC_RW)
    sync_args.flags = DMA_BUF_SYNC_READ;

    if (start) {
        // 开始读取前：Invalidate Cache (抛弃旧缓存，强制从 RAM 读)
        sync_args.flags |= DMA_BUF_SYNC_START;
    } else {
        // 读取结束后
        sync_args.flags |= DMA_BUF_SYNC_END;
    }

    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_args) < 0) {
        perror("DMA_BUF_IOCTL_SYNC failed");
        return -1;
    }
    return 0;
}