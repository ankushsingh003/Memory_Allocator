/**
 * false_sharing_bench.cpp
 *
 * Demonstrates the false-sharing penalty and the fix (cache-line padding).
 *
 * Each thread increments its own counter N times.
 * Two layouts:
 *   shared  — all counters in a plain array (may share cache lines)
 *   padded  — each counter gets its own cache line via alignas(64)
 *
 * Build:
 *   g++ -std=c++20 -O2 -Iinclude bench/false_sharing_bench.cpp -o false_sharing -lpthread
 *
 * Output: results/false_sharing_bench.csv
 */

#include "memlab/common/timer.hpp"
#include "memlab/common/stats.hpp"
#include "memlab/common/affinity.hpp"
#include "memlab/common/cacheline.hpp"

#include <barrier>
#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace memlab;

static constexpr uint64_t kOpsPerThread = 100'000'000ULL;
static constexpr int      kRuns         = 5;
static constexpr int      kMaxThreads   = 16;

// ─── Shared layout (false sharing) ────────────────────────────────────────────
static volatile long shared_counters[kMaxThreads];

// ─── Padded layout (no false sharing) ─────────────────────────────────────────
static padded<long> padded_counters[kMaxThreads];

// ─── Run one configuration ────────────────────────────────────────────────────
template <typename CounterT>
double run_bench(CounterT* counters, int T) {
    std::barrier<> bar(T + 1);

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(T));

    for (int i = 0; i < T; ++i) {
        threads.emplace_back([&, i]() {
            bar.arrive_and_wait();
            for (uint64_t k = 0; k < kOpsPerThread; ++k)
                ++counters[i];
            bar.arrive_and_wait();
        });
    }

    SteadyTimer timer;
    timer.start();
    bar.arrive_and_wait();  // start
    bar.arrive_and_wait();  // stop
    double elapsed_ms = static_cast<double>(timer.elapsed_ms());

    for (auto& t : threads) t.join();
    return elapsed_ms;
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    unsigned ncores = hardware_concurrency();
    int max_t = std::min(kMaxThreads, static_cast<int>(ncores));

    CsvWriter csv("results/false_sharing_bench.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"threads","layout","run","elapsed_ms","throughput_mops"});

    int thread_counts[] = {1, 2, 4, 8, 16};

    for (int T : thread_counts) {
        if (T > max_t) break;
        printf("=== T=%d ===\n", T);

        for (int run = 0; run < kRuns; ++run) {
            // Reset
            for (int i = 0; i < T; ++i) {
                shared_counters[i]  = 0;
                padded_counters[i]  = padded<long>(0);
            }

            double ms_shared = run_bench(shared_counters, T);
            double ms_padded = run_bench(padded_counters, T);

            double total_ops = static_cast<double>(kOpsPerThread) * T;
            double tp_shared = total_ops / ms_shared / 1000.0;
            double tp_padded = total_ops / ms_padded / 1000.0;

            printf("  Run %d: shared=%.1f ms (%.0f MOps/s)  padded=%.1f ms (%.0f MOps/s)"
                   "  speedup=%.2fx\n",
                   run + 1,
                   ms_shared, tp_shared,
                   ms_padded, tp_padded,
                   ms_shared / ms_padded);

            csv.write_row(T, "shared", run + 1, ms_shared, tp_shared);
            csv.write_row(T, "padded", run + 1, ms_padded, tp_padded);
        }
        printf("\n");
    }

    printf("Results -> results/false_sharing_bench.csv\n");
    return 0;
}
