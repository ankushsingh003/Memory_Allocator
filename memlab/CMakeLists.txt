cmake_minimum_required(VERSION 3.15)
project(memlab)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force optimization in Release mode
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Platform-specific thread library
find_package(Threads REQUIRED)

add_library(memlab_common INTERFACE)
target_include_directories(memlab_common INTERFACE include)
target_link_libraries(memlab_common INTERFACE Threads::Threads)

# ── Benchmarks ───────────────────────────────────────────────────────────────

add_executable(prefault_bench bench/prefault_bench.cpp)
target_link_libraries(prefault_bench memlab_common)

add_executable(thread_scaling_bench bench/thread_scaling_bench.cpp)
target_link_libraries(thread_scaling_bench memlab_common)

add_executable(ctx_switch_bench bench/ctx_switch_bench.cpp)
target_link_libraries(ctx_switch_bench memlab_common)

add_executable(false_sharing_bench bench/false_sharing_bench.cpp)
target_link_libraries(false_sharing_bench memlab_common)

add_executable(alloc_contention_bench bench/alloc_contention_bench.cpp)
target_link_libraries(alloc_contention_bench memlab_common)

# ── Simulators ───────────────────────────────────────────────────────────────

add_executable(sched_sim_main sim/sched_sim_main.cpp)
target_link_libraries(sched_sim_main memlab_common)

add_executable(vm_sim_main sim/vm_sim_main.cpp)
target_link_libraries(vm_sim_main memlab_common)
