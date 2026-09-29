/**
 * prefault_bench.cpp
 *
 * Measures first-touch latency (per page) for each PrefaultPolicy.
 *
 * Build (Linux):
 *   g++ -std=c++20 -O2 -Iinclude bench/prefault_bench.cpp -o prefault_bench -lpthread
 *
 * Run:
 *   ./prefault_bench
 *   # TLB/fault stats:
 *   perf stat -e dTLB-load-misses,page-faults ./prefault_bench
 *
 * Output:
 *   results/prefault_bench.csv
 */

#include "memlab/common/timer.hpp"
#include "memlab/common/stats.hpp"
#include "memlab/common/affinity.hpp"
#include "memlab/vm/vmem_region.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <array>
#include <cstdint>

#if defined(_WIN32)
  #include <windows.h>
  static long page_bytes() { SYSTEM_INFO si; GetSystemInfo(&si); return si.dwPageSize; }
#else
  #include <unistd.h>
  static long page_bytes() { return ::sysconf(_SC_PAGESIZE); }
#endif

using namespace memlab;

// ─── Configuration ────────────────────────────────────────────────────────────
static constexpr size_t kRegionBytes  = 256ULL * 1024 * 1024; // 256 MB (1 GB on Linux)
static constexpr int    kWarmupPages  = 64;   // pages touched before timing starts
static constexpr int    kRuns         = 3;    // repeat each policy N times

struct BenchResult {
    std::string policy_name;
    int64_t     startup_ns;
    Percentiles latency;        // per-page first-touch latency
    long        minor_faults;
    long        major_faults;
};

// ─── Prevent dead-code elimination ────────────────────────────────────────────
static volatile long sink = 0;
template<typename T>
inline void do_not_optimize(T& v) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" : "+r"(v));
#else
    sink = (long)(uintptr_t)(&v);
#endif
}

// ─── Single policy run ────────────────────────────────────────────────────────
BenchResult run_policy(const std::string& name, PrefaultPolicy policy) {
    const long   ps         = page_bytes();
    const size_t num_pages  = kRegionBytes / static_cast<size_t>(ps);

    // Pre-allocate latency array — no allocations inside timed loop
    std::vector<int64_t> latencies(num_pages);

    // ── Startup cost ──────────────────────────────────────────────────────
    auto before_faults = FaultStats::capture();
    int64_t startup_ns = 0;
    {
        TimerGuard tg(&startup_ns);
        VmemRegion region(kRegionBytes, policy);
        (void)region; // construction is the measured event for startup

        // ── Warm-up ───────────────────────────────────────────────────────
        volatile char* base = static_cast<volatile char*>(region.base());
        for (int i = 0; i < kWarmupPages; ++i)
            base[static_cast<size_t>(i) * static_cast<size_t>(ps)] = 1;

        // ── Per-page first-touch latency ──────────────────────────────────
        // For Lazy: each touch here IS the first touch -> we see the fault cost.
        // For Eager/Populate: pages already faulted -> we see only TLB + cache cost.
        for (size_t pg = static_cast<size_t>(kWarmupPages); pg < num_pages; ++pg) {
            uint64_t t0 = rdtsc_start();
            base[pg * static_cast<size_t>(ps)] = static_cast<char>(pg & 0xFF);
            uint64_t t1 = rdtsc_stop();
            latencies[pg] = static_cast<int64_t>(t1 - t0);  // ticks (convert later)
        }

        do_not_optimize(base);
    }
    auto after_faults = FaultStats::capture();
    auto fault_delta  = after_faults - before_faults;

    return BenchResult{
        name,
        startup_ns,
        compute_percentiles(latencies),
        fault_delta.minor_faults,
        fault_delta.major_faults
    };
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    // Pin to core 0 to reduce migration noise
    try { pin_thread_to_core(0); } catch (...) {}

    // Calibrate TSC once
    double ticks_per_ns = tsc_ticks_per_ns();
    printf("TSC calibration: %.3f ticks/ns\n\n", ticks_per_ns);

    struct PolicyEntry {
        std::string    name;
        PrefaultPolicy policy;
    };

    std::array<PolicyEntry, 3> policies = {{
        { "Lazy",         PrefaultPolicy::Lazy         },
        { "EagerTouch",   PrefaultPolicy::EagerTouch   },
        { "MapPopulate",  PrefaultPolicy::MapPopulate  },
    }};

    // Results CSV
    CsvWriter csv("results/prefault_bench.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"policy","run",
                      "startup_ms",
                      "p50_ticks","p99_ticks","p999_ticks",
                      "p50_ns","p99_ns","p999_ns",
                      "minor_faults","major_faults"});

    for (auto& [name, pol] : policies) {
        printf("=== %s ===\n", name.c_str());

        for (int run = 0; run < kRuns; ++run) {
            BenchResult r = run_policy(name, pol);

            double p50_ns  = r.latency.p50  / ticks_per_ns;
            double p99_ns  = r.latency.p99  / ticks_per_ns;
            double p999_ns = r.latency.p999 / ticks_per_ns;

            printf("  Run %d: startup=%.1f ms  p50=%.1f ns  p99=%.1f ns  p99.9=%.1f ns"
                   "  minflt=%ld  majflt=%ld\n",
                   run + 1,
                   static_cast<double>(r.startup_ns) / 1e6,
                   p50_ns, p99_ns, p999_ns,
                   r.minor_faults, r.major_faults);

            csv.write_row(name, run + 1,
                          static_cast<double>(r.startup_ns) / 1e6,
                          r.latency.p50, r.latency.p99, r.latency.p999,
                          p50_ns, p99_ns, p999_ns,
                          r.minor_faults, r.major_faults);
        }
        printf("\n");
    }

    printf("Results written to results/prefault_bench.csv\n");
    return 0;
}
