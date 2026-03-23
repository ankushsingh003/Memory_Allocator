#include <benchmark/benchmark.h>
#include <vector>
#include <cstdlib>

static void BM_MallocFree(benchmark::State& state) {
    for (auto _ : state) {
        void* ptr = std::malloc(state.range(0));
        std::free(ptr);
    }
}
BENCHMARK(BM_MallocFree)->Arg(64)->Arg(1024)->Arg(4096);

BENCHMARK_MAIN();
