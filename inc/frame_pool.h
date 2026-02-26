#pragma once
#include <vector>
#include <mutex>
#include <memory>
#include <cstdint>

class FramePool {
public:
    FramePool(size_t block_size, size_t prealloc)
        : block_size_(block_size) {
        for (size_t i = 0; i < prealloc; ++i) {
            free_.push_back(new uint8_t[block_size_]);
        }
    }

    ~FramePool() {
        for (auto p : free_) delete[] p;
    }

    std::shared_ptr<uint8_t> acquire(size_t need) {
        if (need > block_size_) {
            return std::shared_ptr<uint8_t>(new uint8_t[need], std::default_delete<uint8_t[]>());
        }

        uint8_t* p = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!free_.empty()) {
                p = free_.back();
                free_.pop_back();
            }
        }
        if (!p) p = new uint8_t[block_size_];

        return std::shared_ptr<uint8_t>(p, [this](uint8_t* ptr) {
            std::lock_guard<std::mutex> lk(m_);
            free_.push_back(ptr);
        });
    }

private:
    size_t block_size_;
    std::vector<uint8_t*> free_;
    std::mutex m_;
};