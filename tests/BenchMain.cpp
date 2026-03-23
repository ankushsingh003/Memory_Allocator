#include <benchmark/benchmark.h>
#include "PoolAllocator.hpp"
#include "LinearArena.hpp"
#include "SlabCache.hpp"
#include <vector>
#include <memory>

// 1. Malloc/Free Baseline
static void BM_MallocFree(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = std::malloc(sizeof(int));
        benchmark::DoNotOptimize(ptr);
        std::free(ptr);
    }
}
BENCHMARK(BM_MallocFree);

// 2. PoolAllocator Benchmark
static void BM_PoolAllocator(benchmark::State& state) {
    Memory::PoolAllocator<int> pool(10000);
    for (auto _ : state) {
        int* ptr = pool.Allocate();
        benchmark::DoNotOptimize(ptr);
        pool.Free(ptr);
    }
}
BENCHMARK(BM_PoolAllocator);

// 3. LinearArena Benchmark (Sequential Allocation)
static void BM_LinearArena(benchmark::State& state) {
    Memory::LinearArena arena(1024 * 1024); // 1MB
    for (auto _ : state) {
        void* ptr = arena.Allocate(sizeof(int));
        benchmark::DoNotOptimize(ptr);
        if (arena.GetUsedMemory() > 1000000) {
            arena.Reset();
        }
    }
}
BENCHMARK(BM_LinearArena);

// 4. SlabCache Benchmark
static void BM_SlabCache(benchmark::State& state) {
    Memory::SlabCache<int> cache(1024);
    for (auto _ : state) {
        int* ptr = cache.Allocate();
        benchmark::DoNotOptimize(ptr);
        cache.Free(ptr);
    }
}
BENCHMARK(BM_SlabCache);

BENCHMARK_MAIN();
