#include "blazekv/arena.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

using blazekv::BumpArena;
using blazekv::SlabAllocator;

TEST(SlabAllocator, SmallAllocationsReuseFreeList) {
    SlabAllocator sa;
    void* a = sa.allocate(24);
    ASSERT_NE(a, nullptr);
    std::memset(a, 0xAB, 24);
    sa.deallocate(a, 24);
    // The next same-class allocation should reuse the freed block.
    void* b = sa.allocate(24);
    EXPECT_EQ(a, b);
    sa.deallocate(b, 24);
}

TEST(SlabAllocator, LargeAllocationsBypassSlabs) {
    SlabAllocator sa;
    void* big = sa.allocate(4096);
    ASSERT_NE(big, nullptr);
    std::memset(big, 1, 4096);
    sa.deallocate(big, 4096);
}

TEST(SlabAllocator, ManyDistinctBlocks) {
    SlabAllocator sa;
    std::vector<void*> ptrs;
    for (int i = 0; i < 10000; ++i) ptrs.push_back(sa.allocate(64));
    // All live pointers must be distinct.
    std::sort(ptrs.begin(), ptrs.end());
    EXPECT_EQ(std::unique(ptrs.begin(), ptrs.end()), ptrs.end());
    for (void* p : ptrs) sa.deallocate(p, 64);
}

TEST(BumpArena, MonotonicAndReset) {
    BumpArena arena(1024);
    void* a = arena.allocate(100);
    void* b = arena.allocate(100);
    EXPECT_NE(a, b);
    arena.reset();
    void* c = arena.allocate(100);
    EXPECT_EQ(a, c);  // reset rewinds to the first block
}
