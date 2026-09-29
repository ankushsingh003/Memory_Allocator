/**
 * thread_scaling_bench.cpp
 *
 * Measures throughput of a fixed work-unit split across T threads.
 * Work: XOR-hash 400M uint32_t values (embarrassingly parallel).
 *
 * Sweep T = 1, 2, 4, 8, 16, 32, 64
 * Reports: speedup, efficiency, voluntary/involuntary context switches.
 *
 * Build:
 *   g++ -std=c++20 -O2 -Iinclude bench/thread_scaling_bench.cpp -o thread_scaling -lpthread
 *
 * Output: results/thread_scaling_bench.csv
 */

#include "memlab/common/timer.hpp"
#include "memlab/common/stats.hpp"
#include "memlab/common/affinity.hpp"
#include "memlab/common/cacheline.hpp"

#include <barrier>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

#if defined(__linux__)
  #include <sys/resource.h>
#endif

using namespace memlab;

// ─── Configuration ────────────────────────────────────────────────────────────
static constexpr uint64_t kTotalItems = 400'000'000ULL;  // total uint32_t to hash
static constexpr int      kRuns       = 5;

// ─── Work kernel ──────────────────────────────────────────────────────────────
// XOR-hash over a slice. The result is accumulated to prevent dead-code removal.
struct alignas(kCacheLine) ThreadResult {
    uint32_t checksum{0};
    long     vcsw{0};       // voluntary context switches
    long     ivcsw{0};      // involuntary context switches
};

static void hash_work(const uint32_t* data, uint64_t count, ThreadResult& out) {
#if defined(__linux__)
    struct rusage ru0{};
    ::getrusage(RUSAGE_THREAD, &ru0);
#endif

    uint32_t acc = 0;
    for (uint64_t i = 0; i < count; ++i) {
        acc ^= data[i] * 2654435761u;  // Knuth multiplicative hash
    }
    out.checksum = acc;

#if defined(__linux__)
    struct rusage ru1{};
    ::getrusage(RUSAGE_THREAD, &ru1);
    out.vcsw  = ru1.ru_nvcsw  - ru0.ru_nvcsw;
    out.ivcsw = ru1.ru_nivcsw - ru0.ru_nivcsw;
#endif
}

// ─── Run one scaling point ────────────────────────────────────────────────────
struct ScalingResult {
    int      threads;
    double   elapsed_ms;
    double   throughput_mops;   // million ops / second
    double   speedup;
    double   efficiency;
    long     total_vcsw;
    long     total_ivcsw;
};

static double g_t1_ms = 0.0;   // single-thread baseline (set on first run)

ScalingResult run_scaling(const uint32_t* data, int T) {
    uint64_t per_thread = kTotalItems / static_cast<uint64_t>(T);

    std::vector<std::thread>    threads;
    std::vector<ThreadResult>   results(static_cast<size_t>(T));

    // barrier: all threads wait until all are created, then start together
    std::barrier<> bar(T + 1);   // +1 for the timing thread (main)

    threads.reserve(static_cast<size_t>(T));
    for (int i = 0; i < T; ++i) {
        uint64_t start = static_cast<uint64_t>(i) * per_thread;
        uint64_t count = (i == T - 1)
            ? (kTotalItems - start)     // last thread takes remainder
            : per_thread;

        threads.emplace_back([&, i, start, count]() {
            bar.arrive_and_wait();      // synchronise with main
            hash_work(data + start, count, results[static_cast<size_t>(i)]);
            bar.arrive_and_wait();      // signal done
        });
    }

    // Timing: from first thread start to last thread finish
    SteadyTimer timer;
    timer.start();
    bar.arrive_and_wait();   // release all workers
    bar.arrive_and_wait();   // wait for all to finish
    double elapsed_ms = static_cast<double>(timer.elapsed_ms());

    for (auto& t : threads) t.join();

    // Aggregate
    long vcsw = 0, ivcsw = 0;
    for (auto& r : results) { vcsw += r.vcsw; ivcsw += r.ivcsw; }

    if (T == 1) g_t1_ms = elapsed_ms;

    double throughput = static_cast<double>(kTotalItems) / elapsed_ms / 1000.0; // MOps/s
    double speedup    = g_t1_ms / elapsed_ms;
    double efficiency = speedup / static_cast<double>(T);

    return { T, elapsed_ms, throughput, speedup, efficiency, vcsw, ivcsw };
}

// ─── Amdahl fit ───────────────────────────────────────────────────────────────
// speedup(T) = 1 / (s + (1-s)/T)  => s = (1/speedup - 1/T) / (1 - 1/T)
static double estimate_serial_fraction(const std::vector<ScalingResult>& results) {
    double sum_s = 0; int count = 0;
    for (auto& r : results) {
        if (r.threads <= 1 || r.speedup <= 0) continue;
        double inv_T = 1.0 / r.threads;
        double inv_S = 1.0 / r.speedup;
        double denom = 1.0 - inv_T;
        if (denom < 1e-9) continue;
        sum_s += (inv_S - inv_T) / denom;
        ++count;
    }
    return (count > 0) ? (sum_s / count) : 0.0;
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    // Generate work data once (read-only, shared across threads — no cache-line issues)
    printf("Allocating %.0f MB work buffer...\n",
           kTotalItems * sizeof(uint32_t) / 1024.0 / 1024.0);
    std::vector<uint32_t> data(kTotalItems);
    for (uint64_t i = 0; i < kTotalItems; ++i)
        data[i] = static_cast<uint32_t>(i * 6364136223846793005ULL >> 33); // LCG
    printf("Done.\n\n");

    CsvWriter csv("results/thread_scaling_bench.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"threads","run","elapsed_ms","throughput_mops",
                      "speedup","efficiency","vcsw","ivcsw"});

    int thread_counts[] = {1, 2, 4, 8, 16, 32, 64};
    unsigned max_cores  = hardware_concurrency();

    std::vector<ScalingResult> all_results;

    for (int T : thread_counts) {
        if (static_cast<unsigned>(T) > max_cores * 2) break; // don't go crazy

        printf("--- T=%d ---\n", T);
        for (int run = 0; run < kRuns; ++run) {
            auto r = run_scaling(data.data(), T);
            printf("  Run %d: %.1f ms  %.1f MOps/s  speedup=%.2fx  eff=%.1f%%"
                   "  vcsw=%ld  ivcsw=%ld\n",
                   run + 1, r.elapsed_ms, r.throughput_mops,
                   r.speedup, r.efficiency * 100.0,
                   r.total_vcsw, r.total_ivcsw);
            csv.write_row(T, run + 1, r.elapsed_ms, r.throughput_mops,
                          r.speedup, r.efficiency,
                          r.total_vcsw, r.total_ivcsw);
            if (run == kRuns / 2) all_results.push_back(r);
        }
    }

    double serial = estimate_serial_fraction(all_results);
    printf("\nEstimated serial fraction (Amdahl): %.2f%%\n", serial * 100.0);
    printf("Results -> results/thread_scaling_bench.csv\n");
    return 0;
}
