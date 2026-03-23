#include <gtest/gtest.h>
#include "MemoryUtils.hpp"
#include "VirtualMemory.hpp"
#include "PoolAllocator.hpp"

// ... (other tests)

TEST(PoolAllocatorTest, BasicAllocation) {
    struct Vec3 { float x, y, z; };
    Memory::PoolAllocator<Vec3> pool(100);

    Vec3* p1 = pool.Allocate();
    ASSERT_NE(p1, nullptr);
    
    p1->x = 1.0f;
    EXPECT_EQ(p1->x, 1.0f);

    pool.Free(p1);
}

TEST(PoolAllocatorTest, FullPoolTest) {
    Memory::PoolAllocator<int> pool(2);
    
    int* p1 = pool.Allocate();
    int* p2 = pool.Allocate();
    int* p3 = pool.Allocate();

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p3, nullptr); // Pool should be empty
}

TEST(PoolAllocatorTest, ReuseTest) {
    Memory::PoolAllocator<double> pool(1);
    
    double* p1 = pool.Allocate();
    pool.Free(p1);
    
    double* p2 = pool.Allocate();
    EXPECT_EQ(p1, p2); // Should reuse the exact same memory slot
}

TEST(PoolAllocatorTest, AlignmentTest) {
    // Force a 64-byte alignment (typical cache line)
    Memory::PoolAllocator<int, 64> pool(10);
    
    int* p1 = pool.Allocate();
    int* p2 = pool.Allocate();
    
    // Check if the pointers are multiples of 64
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p1) % 64, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0);
    
    // Distance between them should be at least 64 bytes
    EXPECT_GE(reinterpret_cast<uint8_t*>(p2) - reinterpret_cast<uint8_t*>(p1), 64);
}

TEST(PoolAllocatorTest, StressTest) {
    const size_t count = 1000;
    Memory::PoolAllocator<size_t> pool(count);
    std::vector<size_t*> ptrs;

    // 1. Fill the pool
    for (size_t i = 0; i < count; ++i) {
        size_t* p = pool.Allocate();
        ASSERT_NE(p, nullptr);
        *p = i;
        ptrs.push_back(p);
    }

    // 2. Verify everything is correct
    for (size_t i = 0; i < count; ++i) {
        EXPECT_EQ(*ptrs[i], i);
    }

    // 3. Free in reverse order
    for (int i = count - 1; i >= 0; --i) {
        pool.Free(ptrs[i]);
    }

    // 4. Allocation should still work perfectly after total drain
    size_t* last = pool.Allocate();
    ASSERT_NE(last, nullptr);
    pool.Free(last);
}

TEST(PoolAllocatorTest, OOM_EdgeCase) {
    Memory::PoolAllocator<int> pool(1);
    
    ASSERT_NE(pool.Allocate(), nullptr);
    EXPECT_EQ(pool.Allocate(), nullptr); // Should return nullptr immediately
}
