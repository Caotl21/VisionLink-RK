#ifndef DMA_UTILS_H
#define DMA_UTILS_H

#include <stddef.h> // for size_t

// 管理 DMA 内存的结构体
struct DmaBuffer {
    int fd;         // DMA BUFFER 的文件描述符 (给 RGA 用)
    void *vaddr;    // 映射到用户态的虚拟地址 (给 CPU/OpenCV 用)
    size_t size;    // 内存大小
};

/**
 * @brief 分配 DMA 内存 (Uncached/Write-Combined)
 * @param size 需要分配的大小 (字节)
 * @param buf  输出参数，填充好的 DmaBuffer 结构体
 * @return 0 成功, -1 失败
 */
int alloc_dma_buffer(size_t size, struct DmaBuffer *buf);

/**
 * @brief 释放 DMA 内存
 * @param buf 需要释放的 DmaBuffer 结构体指针
 */
void free_dma_buffer(struct DmaBuffer *buf);
int dma_sync_cpu(int fd, bool start);

#endif // DMA_UTILS_H