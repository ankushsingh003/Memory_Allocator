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
