cmake_minimum_required(VERSION 3.20)
project(CustomMemoryAllocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Main library
file(GLOB_RECURSE SRC_FILES src/*.cpp)
add_library(MemoryAllocator ${SRC_FILES})

# Test executable
file(GLOB_RECURSE TEST_FILES tests/*.cpp)
add_executable(MemoryAllocatorTests ${TEST_FILES})
target_link_libraries(MemoryAllocatorTests PRIVATE MemoryAllocator)

# Enable testing
enable_testing()
add_test(NAME AllTests COMMAND MemoryAllocatorTests)
