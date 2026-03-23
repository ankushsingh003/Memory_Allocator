#include <gtest/gtest.h>
#include "MemoryUtils.hpp"

TEST(MemoryUtilsTest, AlignmentTest) {
    size_t size = 15;
    size_t alignment = 8;
    EXPECT_EQ(Memory::AlignUp(size, alignment), 16);
}

TEST(MemoryUtilsTest, VirtualAllocTest) {
    size_t size = 4096; // 4KB page
    void* ptr = Memory::AllocateVirtualMemory(size);
    ASSERT_NE(ptr, nullptr);
    Memory::FreeVirtualMemory(ptr, size);
}
