#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "blazekv/common.hpp"

namespace blazekv {

// A shard-local slab allocator with small-object optimization.
//
// Small requests (<= kMaxSmall) are served from per-size-class free lists carved
// out of large mmap-friendly slabs, which keeps hot metadata (dict entries, small
// values) densely packed and cache friendly. Large requests fall through to the
// system allocator. Because each shard owns its own SlabAllocator there is no
// synchronization on the fast path: the shared-nothing model turns allocation into
// a thread-local bump/pop operation.
class SlabAllocator {
   public:
    static constexpr std::size_t kAlign = 16;
    static constexpr std::size_t kMaxSmall = 512;
    static constexpr std::size_t kSlabSize = 1u << 20;  // 1 MiB slabs
    static constexpr std::size_t kNumClasses = kMaxSmall / kAlign;

    SlabAllocator() { free_lists_.fill(nullptr); }
    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    ~SlabAllocator() {
        for (void* slab : slabs_) std::free(slab);
    }

    [[nodiscard]] void* allocate(std::size_t bytes) {
        if (BLAZEKV_UNLIKELY(bytes == 0)) bytes = 1;
        if (BLAZEKV_UNLIKELY(bytes > kMaxSmall)) {
            large_bytes_ += bytes;
            return ::operator new(bytes);
        }
        const std::size_t cls = size_class(bytes);
        Node* head = free_lists_[cls];
        if (BLAZEKV_UNLIKELY(head == nullptr)) {
            head = refill(cls);
        }
        free_lists_[cls] = head->next;
        live_bytes_ += (cls + 1) * kAlign;
        return head;
    }

    void deallocate(void* p, std::size_t bytes) noexcept {
        if (p == nullptr) return;
        if (BLAZEKV_UNLIKELY(bytes == 0)) bytes = 1;
        if (BLAZEKV_UNLIKELY(bytes > kMaxSmall)) {
            large_bytes_ -= bytes;
            ::operator delete(p);
            return;
        }
        const std::size_t cls = size_class(bytes);
        auto* node = static_cast<Node*>(p);
        node->next = free_lists_[cls];
        free_lists_[cls] = node;
        live_bytes_ -= (cls + 1) * kAlign;
    }

    // Bytes currently handed out to callers (small-object pool only).
    [[nodiscard]] std::size_t live_bytes() const noexcept { return live_bytes_; }
    // Total slab memory reserved from the OS.
    [[nodiscard]] std::size_t reserved_bytes() const noexcept {
        return slabs_.size() * kSlabSize + large_bytes_;
    }

   private:
    struct Node {
        Node* next;
    };

    static constexpr std::size_t size_class(std::size_t bytes) noexcept {
        return (bytes + kAlign - 1) / kAlign - 1;
    }

    Node* refill(std::size_t cls) {
        const std::size_t obj = (cls + 1) * kAlign;
        // malloc returns memory aligned to alignof(max_align_t) (>= 16 on the
        // targets we support), which satisfies kAlign without aligned_alloc.
        auto* slab = static_cast<std::uint8_t*>(std::malloc(kSlabSize));
        slabs_.push_back(slab);
        const std::size_t count = kSlabSize / obj;
        Node* prev = nullptr;
        for (std::size_t i = 0; i < count; ++i) {
            auto* node = reinterpret_cast<Node*>(slab + i * obj);
            node->next = prev;
            prev = node;
        }
        free_lists_[cls] = prev;
        return prev;
    }

    std::array<Node*, kNumClasses> free_lists_{};
    std::vector<void*> slabs_;
    std::size_t live_bytes_ = 0;
    std::size_t large_bytes_ = 0;
};

// A monotonic bump arena for objects that share a lifetime (e.g. a single request
// scratch space, or a snapshot builder). Frees everything at once in O(1).
class BumpArena {
   public:
    explicit BumpArena(std::size_t block = 64 * 1024) : block_size_(block) {}
    BumpArena(const BumpArena&) = delete;
    BumpArena& operator=(const BumpArena&) = delete;
    ~BumpArena() {
        for (auto& b : blocks_) std::free(b.base);
    }

    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t align = alignof(std::max_align_t)) {
        std::size_t off = (offset_ + align - 1) & ~(align - 1);
        if (BLAZEKV_UNLIKELY(cur_ == nullptr || off + bytes > cur_->size)) {
            grow(bytes + align);
            off = (offset_ + align - 1) & ~(align - 1);
        }
        void* p = cur_->base + off;
        offset_ = off + bytes;
        return p;
    }

    void reset() noexcept {
        if (!blocks_.empty()) {
            cur_ = &blocks_.front();
            offset_ = 0;
        }
    }

   private:
    struct Block {
        std::uint8_t* base;
        std::size_t size;
    };

    void grow(std::size_t need) {
        std::size_t sz = block_size_ > need ? block_size_ : need;
        auto* base = static_cast<std::uint8_t*>(std::malloc(sz));
        blocks_.push_back({base, sz});
        cur_ = &blocks_.back();
        offset_ = 0;
    }

    std::size_t block_size_;
    std::vector<Block> blocks_;
    Block* cur_ = nullptr;
    std::size_t offset_ = 0;
};

}  // namespace blazekv
