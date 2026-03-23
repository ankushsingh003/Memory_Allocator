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
