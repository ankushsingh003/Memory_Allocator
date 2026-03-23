#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "MemoryUtils.hpp"
#include "VirtualMemory.hpp"
#include "PoolAllocator.hpp"
#include "LinearArena.hpp"

// -----------------------------------------------------------------------------
// PoolAllocator Tests
// -----------------------------------------------------------------------------

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
    EXPECT_EQ(p3, nullptr);
}

TEST(PoolAllocatorTest, ReuseTest) {
    Memory::PoolAllocator<double> pool(1);
    
    double* p1 = pool.Allocate();
    pool.Free(p1);
    
    double* p2 = pool.Allocate();
    EXPECT_EQ(p1, p2);
}

TEST(PoolAllocatorTest, AlignmentTest) {
    Memory::PoolAllocator<int, 64> pool(10);
    
    int* p1 = pool.Allocate();
    int* p2 = pool.Allocate();
    
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p1) % 64, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0);
    EXPECT_GE(reinterpret_cast<uint8_t*>(p2) - reinterpret_cast<uint8_t*>(p1), 64);
}

TEST(PoolAllocatorTest, StressTest) {
    const size_t count = 1000;
    Memory::PoolAllocator<size_t> pool(count);
    std::vector<size_t*> ptrs;

    for (size_t i = 0; i < count; ++i) {
        size_t* p = pool.Allocate();
        ASSERT_NE(p, nullptr);
        *p = i;
        ptrs.push_back(p);
    }

    for (size_t i = 0; i < count; ++i) {
        EXPECT_EQ(*ptrs[i], i);
    }

    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
        pool.Free(ptrs[i]);
    }

    size_t* last = pool.Allocate();
    ASSERT_NE(last, nullptr);
    pool.Free(last);
}

// -----------------------------------------------------------------------------
// StackArena Tests
// -----------------------------------------------------------------------------

TEST(StackArenaTest, FastStackAllocation) {
    // 1024 bytes on the STACK
    Memory::StackArena<1024> stack;
    
    void* p1 = stack.Allocate(100);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(stack.GetUsedMemory(), 100);
    
    stack.Reset();
    EXPECT_EQ(stack.GetUsedMemory(), 0);
}

TEST(StackArenaTest, StackOOM) {
    Memory::StackArena<16> tinyStack;
    
    ASSERT_NE(tinyStack.Allocate(16), nullptr);
    EXPECT_EQ(tinyStack.Allocate(1), nullptr); // Should fail immediately
}

// -----------------------------------------------------------------------------
// LinearArena Tests
// -----------------------------------------------------------------------------

TEST(LinearArenaTest, BasicBumpAllocation) {
    Memory::LinearArena arena(1024);
    
    void* p1 = arena.Allocate(100);
    void* p2 = arena.Allocate(200);
    
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_GT(static_cast<uint8_t*>(p2), static_cast<uint8_t*>(p1));
    EXPECT_EQ(arena.GetUsedMemory(), 300);
}

TEST(LinearArenaTest, AlignmentTest) {
    Memory::LinearArena arena(1024);
    
    arena.Allocate(1); 
    void* p2 = arena.Allocate(10, 64);
    
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0);
}

TEST(LinearArenaTest, ResetTest) {
    Memory::LinearArena arena(1024);
    
    arena.Allocate(500);
    arena.Reset();
    
    EXPECT_EQ(arena.GetUsedMemory(), 0);
    void* p1 = arena.Allocate(100);
    ASSERT_NE(p1, nullptr);
}

TEST(LinearArenaTest, ScopeGuardTest) {
    Memory::LinearArena arena(1024);
    {
        Memory::ArenaScope scope(arena);
        arena.Allocate(100);
        EXPECT_EQ(arena.GetUsedMemory(), 100);
    }
    EXPECT_EQ(arena.GetUsedMemory(), 0);
}
