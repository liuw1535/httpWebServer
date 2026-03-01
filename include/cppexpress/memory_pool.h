#pragma once

/**
 * 小对象内存池 - 减少内存碎片，提高分配效率
 * 使用固定大小块的空闲链表实现
 */

#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <memory>
#include <cassert>
#include <new>

namespace cppexpress {

/**
 * 固定大小块内存池
 * 每个池管理固定大小的内存块，使用空闲链表
 */
class FixedSizePool {
public:
    explicit FixedSizePool(size_t blockSize, size_t blocksPerChunk = 64)
        : blockSize_(std::max(blockSize, sizeof(FreeNode)))
        , blocksPerChunk_(blocksPerChunk)
        , freeList_(nullptr)
        , allocCount_(0)
        , freeCount_(0) {
    }

    ~FixedSizePool() {
        for (auto* chunk : chunks_) {
            std::free(chunk);
        }
    }

    FixedSizePool(const FixedSizePool&) = delete;
    FixedSizePool& operator=(const FixedSizePool&) = delete;

    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!freeList_) {
            expandPool();
        }
        FreeNode* node = freeList_;
        freeList_ = node->next;
        ++allocCount_;
        return static_cast<void*>(node);
    }

    void deallocate(void* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        FreeNode* node = static_cast<FreeNode*>(ptr);
        node->next = freeList_;
        freeList_ = node;
        ++freeCount_;
    }

    size_t blockSize() const { return blockSize_; }
    size_t allocCount() const { return allocCount_; }
    size_t freeCount() const { return freeCount_; }

private:
    struct FreeNode {
        FreeNode* next;
    };

    void expandPool() {
        size_t chunkSize = blockSize_ * blocksPerChunk_;
        char* chunk = static_cast<char*>(std::malloc(chunkSize));
        if (!chunk) {
            throw std::bad_alloc();
        }
        chunks_.push_back(chunk);

        // 将所有块串成空闲链表
        for (size_t i = 0; i < blocksPerChunk_; ++i) {
            FreeNode* node = reinterpret_cast<FreeNode*>(chunk + i * blockSize_);
            node->next = freeList_;
            freeList_ = node;
        }
    }

    size_t blockSize_;
    size_t blocksPerChunk_;
    FreeNode* freeList_;
    std::vector<char*> chunks_;
    std::mutex mutex_;
    size_t allocCount_;
    size_t freeCount_;
};

/**
 * 多级内存池 - 管理多种大小的内存块
 * 对于小对象使用内存池，大对象直接使用malloc
 */
class MemoryPool {
public:
    static MemoryPool& instance() {
        static MemoryPool pool;
        return pool;
    }

    void* allocate(size_t size) {
        // 找到合适的池
        size_t index = getSizeClass(size);
        if (index < pools_.size()) {
            return pools_[index]->allocate();
        }
        // 大对象直接malloc
        return std::malloc(size);
    }

    void deallocate(void* ptr, size_t size) {
        if (!ptr) return;
        size_t index = getSizeClass(size);
        if (index < pools_.size()) {
            pools_[index]->deallocate(ptr);
            return;
        }
        std::free(ptr);
    }

private:
    MemoryPool() {
        // 创建不同大小级别的池: 8, 16, 32, 64, 128, 256, 512, 1024
        for (size_t size = 8; size <= 1024; size *= 2) {
            pools_.push_back(std::make_unique<FixedSizePool>(size, 128));
        }
    }

    size_t getSizeClass(size_t size) const {
        if (size <= 8) return 0;
        if (size <= 16) return 1;
        if (size <= 32) return 2;
        if (size <= 64) return 3;
        if (size <= 128) return 4;
        if (size <= 256) return 5;
        if (size <= 512) return 6;
        if (size <= 1024) return 7;
        return pools_.size(); // 超出范围
    }

    std::vector<std::unique_ptr<FixedSizePool>> pools_;
};

/**
 * 使用内存池的分配器适配器
 * 可用于STL容器
 */
template<typename T>
class PoolAllocator {
public:
    using value_type = T;

    PoolAllocator() noexcept = default;

    template<typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        if (n == 1 && sizeof(T) <= 1024) {
            return static_cast<T*>(MemoryPool::instance().allocate(sizeof(T)));
        }
        return static_cast<T*>(std::malloc(n * sizeof(T)));
    }

    void deallocate(T* ptr, size_t n) {
        if (n == 1 && sizeof(T) <= 1024) {
            MemoryPool::instance().deallocate(ptr, sizeof(T));
            return;
        }
        std::free(ptr);
    }

    template<typename U>
    bool operator==(const PoolAllocator<U>&) const noexcept { return true; }

    template<typename U>
    bool operator!=(const PoolAllocator<U>&) const noexcept { return false; }
};

/**
 * 使用内存池的基类 - 继承此类可自动使用内存池
 */
class PoolObject {
public:
    static void* operator new(size_t size) {
        return MemoryPool::instance().allocate(size);
    }

    static void operator delete(void* ptr, size_t size) {
        MemoryPool::instance().deallocate(ptr, size);
    }

    static void* operator new(size_t size, const std::nothrow_t&) noexcept {
        try {
            return MemoryPool::instance().allocate(size);
        } catch (...) {
            return nullptr;
        }
    }

    static void operator delete(void* ptr, size_t size, const std::nothrow_t&) noexcept {
        MemoryPool::instance().deallocate(ptr, size);
    }

    virtual ~PoolObject() = default;
};

} // namespace cppexpress
