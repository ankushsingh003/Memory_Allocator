/**
 * alloc_contention_bench.cpp
 *
 * Compares allocator throughput under multi-threaded contention:
 *   - system malloc
 *   - global mutex wrapper
 *   - (placeholder for PoolAllocator and TLS SlabCache from ../include/alloc/)
 *
 * Each thread does M alloc/free cycles of 64-byte objects.
 *
 * Build:
 *   g++ -std=c++20 -O2 -Iinclude bench/alloc_contention_bench.cpp -o alloc_contention -lpthread
 *
 * Output: results/alloc_contention_bench.csv
 */

#include "memlab/common/timer.hpp"
#include "memlab/common/stats.hpp"
#include "memlab/common/affinity.hpp"
#include "memlab/common/cacheline.hpp"

#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace memlab;

static constexpr int    kAllocsPerThread = 1'000'000;
static constexpr size_t kAllocSize       = 64;        // bytes
static constexpr int    kRuns            = 5;

// ─── Allocator interface (minimal) ───────────────────────────────────────────
struct IAlloc {
    virtual void* alloc(size_t sz) = 0;
    virtual void  free(void* p)    = 0;
    virtual const char* name()     = 0;
    virtual ~IAlloc() = default;
};

// ─── 1. System malloc ─────────────────────────────────────────────────────────
struct MallocAlloc : IAlloc {
    void* alloc(size_t sz) override { return ::malloc(sz); }
    void  free(void* p)    override { ::free(p); }
    const char* name()     override { return "malloc"; }
};

// ─── 2. Global-mutex malloc wrapper ──────────────────────────────────────────
struct MutexAlloc : IAlloc {
    std::mutex mtx;
    void* alloc(size_t sz) override {
        std::lock_guard<std::mutex> lk(mtx);
        return ::malloc(sz);
    }
    void free(void* p) override {
        std::lock_guard<std::mutex> lk(mtx);
        ::free(p);
    }
    const char* name() override { return "mutex_malloc"; }
};

// ─── 3. Per-thread cache (simple TLS free-list stub) ─────────────────────────
// In the real project this would use PoolAllocator / SlabCache from include/alloc/.
// Here we demonstrate the concept with a thread_local free-list.
static constexpr int kTLSCacheSize = 32;

struct TLSAlloc : IAlloc {
    void* alloc(size_t sz) override {
        auto& cache = get_cache();
        if (!cache.empty()) {
            void* p = cache.back(); cache.pop_back();
            return p;
        }
        return ::malloc(sz);
    }
    void free(void* p) override {
        auto& cache = get_cache();
        if (static_cast<int>(cache.size()) < kTLSCacheSize) {
            cache.push_back(p);
        } else {
            ::free(p);
        }
    }
    const char* name() override { return "tls_cache"; }

private:
    static std::vector<void*>& get_cache() {
        thread_local std::vector<void*> cache;
        return cache;
    }
};

// ─── Benchmark one allocator ──────────────────────────────────────────────────
struct alignas(kCacheLine) ThreadStat { double elapsed_ms{0}; };

double bench_allocator(IAlloc& alloc, int T) {
    std::barrier<> bar(T + 1);
    std::vector<ThreadStat> stats(static_cast<size_t>(T));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(T));

    for (int i = 0; i < T; ++i) {
        threads.emplace_back([&, i]() {
            bar.arrive_and_wait();     // start together
            SteadyTimer t; t.start();

            for (int op = 0; op < kAllocsPerThread; ++op) {
                void* p = alloc.alloc(kAllocSize);
                // prevent dead-code elimination
                volatile char* cp = static_cast<volatile char*>(p);
                cp[0] = static_cast<char>(op);
                alloc.free(p);
            }

            stats[static_cast<size_t>(i)].elapsed_ms =
                static_cast<double>(t.elapsed_ms());
            bar.arrive_and_wait();     // done
        });
    }

    SteadyTimer wall; wall.start();
    bar.arrive_and_wait();    // release
    bar.arrive_and_wait();    // wait
    double ms = static_cast<double>(wall.elapsed_ms());

    for (auto& th : threads) th.join();
    return ms;
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    unsigned ncores = hardware_concurrency();

    CsvWriter csv("results/alloc_contention_bench.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"allocator","threads","run","elapsed_ms","throughput_mops"});

    MallocAlloc  ma;
    MutexAlloc   mxa;
    TLSAlloc     tls;

    std::vector<IAlloc*> allocators = {&ma, &mxa, &tls};
    int thread_counts[] = {1, 2, 4, 8, 16};

    for (auto* alloc : allocators) {
        printf("=== %s ===\n", alloc->name());
        for (int T : thread_counts) {
            if (static_cast<unsigned>(T) > ncores * 2) break;
            printf("  T=%d: ", T);

            for (int run = 0; run < kRuns; ++run) {
                double ms = bench_allocator(*alloc, T);
                double total_ops = static_cast<double>(kAllocsPerThread) * T;
                double tp        = total_ops / ms / 1000.0; // MOps/s
                printf("%.1f MOps/s  ", tp);
                csv.write_row(alloc->name(), T, run + 1, ms, tp);
            }
            printf("\n");
        }
        printf("\n");
    }

    printf("Results -> results/alloc_contention_bench.csv\n");
    return 0;
}
