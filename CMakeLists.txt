cmake_minimum_required(VERSION 3.20)
project(MemAllocator CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# -----------------------------------------------------------------------------
# GoogleTest Integration
# -----------------------------------------------------------------------------
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
# For Windows: prevents overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# -----------------------------------------------------------------------------
# Examples
add_executable(packet_demo examples/PacketProcessingDemo.cpp)
target_include_directories(packet_demo PRIVATE include)

# Optional: Add Benchmark target if library is found
# -----------------------------------------------------------------------------
# Google Benchmark Integration
# -----------------------------------------------------------------------------
FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/heads/main.zip
)
# Disable benchmark tests and examples to speed up build
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# -----------------------------------------------------------------------------
# Main Library and Includes
# -----------------------------------------------------------------------------
include_directories(include)

# Find all source files
file(GLOB_RECURSE SOURCES src/*.cpp)

# -----------------------------------------------------------------------------
# Build Targets based on the Roadmap Sanitizers
# -----------------------------------------------------------------------------

# 1. AddressSanitizer & UndefinedBehaviorSanitizer build
add_executable(alloc_asan ${SOURCES} tests/TestMain.cpp)
target_compile_options(alloc_asan PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
target_link_options(alloc_asan PRIVATE -fsanitize=address,undefined)
target_link_libraries(alloc_asan PRIVATE gtest_main)

# 2. ThreadSanitizer build
add_executable(alloc_tsan ${SOURCES} tests/TestMain.cpp)
target_compile_options(alloc_tsan PRIVATE -fsanitize=thread -g)
target_link_options(alloc_tsan PRIVATE -fsanitize=thread)
target_link_libraries(alloc_tsan PRIVATE gtest_main)

# 3. Release: Benchmark build with max optimization
add_executable(alloc_bench ${SOURCES} tests/BenchMain.cpp)
target_compile_options(alloc_bench PRIVATE -O3 -march=native -DNDEBUG)
target_link_libraries(alloc_bench PRIVATE benchmark::benchmark_main)

# -----------------------------------------------------------------------------
# Enable Testing
# -----------------------------------------------------------------------------
enable_testing()
add_test(NAME AllTests COMMAND alloc_asan)
