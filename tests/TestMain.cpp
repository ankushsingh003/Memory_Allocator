#include <gtest/gtest.h>
#include "MemoryUtils.hpp"
#include "VirtualMemory.hpp"

TEST(MemoryUtilsTest, AlignmentTest) {
    size_t size = 15;
    size_t alignment = 8;
    EXPECT_EQ(Memory::AlignUp(size, alignment), 16);
}

TEST(VirtualMemoryTest, RAII_AllocationTest) {
    size_t size = 4096;
    {
        Memory::VirtualMemory vm(size);
        ASSERT_NE(vm.GetPtr(), nullptr);
        EXPECT_GE(vm.GetSize(), size);
        // Memory is automatically freed when 'vm' goes out of scope
    }
}
