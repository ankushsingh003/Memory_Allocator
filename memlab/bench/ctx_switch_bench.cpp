/**
 * ctx_switch_bench.cpp
 *
 * Measures the real cost of a context switch via thread ping-pong.
 * Method: two threads pass a token back and forth through a
 * condition variable (or pipe). 1M round trips -> cost = total / (2 * 1M).
 *
 * Configurations:
 *   same_core   — both threads pinned to core 0
 *   diff_core   — thread 0 on core 0, thread 1 on core 1
 *
 * Build:
 *   g++ -std=c++20 -O2 -Iinclude bench/ctx_switch_bench.cpp -o ctx_switch -lpthread
 *
 * Output: results/ctx_switch_bench.csv
 */

#include "memlab/common/timer.hpp"
#include "memlab/common/stats.hpp"
#include "memlab/common/affinity.hpp"

#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

using namespace memlab;

static constexpr int kRoundTrips = 1'000'000;
static constexpr int kRuns       = 5;

// ─── Ping-pong via condition variable ─────────────────────────────────────────
struct PingPong {
    std::mutex              mtx;
    std::condition_variable cv;
    int                     turn{0};   // 0 = ping's turn, 1 = pong's turn
    bool                    done{false};
};

static double ping_pong_ns(int core_ping, int core_pong) {
    PingPong pp;

    // pong thread
    std::thread pong([&]() {
        try { pin_thread_to_core(static_cast<unsigned>(core_pong)); } catch (...) {}
        for (int i = 0; i < kRoundTrips; ++i) {
            std::unique_lock<std::mutex> lk(pp.mtx);
            pp.cv.wait(lk, [&]{ return pp.turn == 1 || pp.done; });
            if (pp.done) break;
            pp.turn = 0;
            lk.unlock();
            pp.cv.notify_one();
        }
    });

    try { pin_thread_to_core(static_cast<unsigned>(core_ping)); } catch (...) {}

    SteadyTimer timer;
    timer.start();

    for (int i = 0; i < kRoundTrips; ++i) {
        {
            std::unique_lock<std::mutex> lk(pp.mtx);
            pp.turn = 1;
        }
        pp.cv.notify_one();
        {
            std::unique_lock<std::mutex> lk(pp.mtx);
            pp.cv.wait(lk, [&]{ return pp.turn == 0; });
        }
    }

    int64_t total_ns = timer.elapsed_ns();

    {
        std::lock_guard<std::mutex> lk(pp.mtx);
        pp.done = true;
    }
    pp.cv.notify_all();
    pong.join();

    // Each round trip = 2 context switches
    return static_cast<double>(total_ns) / (2.0 * kRoundTrips);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    unsigned ncores = hardware_concurrency();

    struct Config {
        std::string name;
        int core_a, core_b;
    };

    std::vector<Config> configs = {
        { "same_core",  0, 0 },
    };
    if (ncores >= 2) {
        configs.push_back({ "diff_core", 0, 1 });
    }

    CsvWriter csv("results/ctx_switch_bench.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"config","run","cost_ns"});

    printf("Context-switch ping-pong benchmark (%d round trips each)\n\n", kRoundTrips);

    for (auto& cfg : configs) {
        printf("=== %s (core %d <-> core %d) ===\n",
               cfg.name.c_str(), cfg.core_a, cfg.core_b);

        std::vector<double> samples;
        for (int run = 0; run < kRuns; ++run) {
            double cost_ns = ping_pong_ns(cfg.core_a, cfg.core_b);
            printf("  Run %d: %.1f ns/switch\n", run + 1, cost_ns);
            samples.push_back(cost_ns);
            csv.write_row(cfg.name, run + 1, cost_ns);
        }

        // Summary
        double sum = 0; for (auto v : samples) sum += v;
        double med = samples[samples.size()/2];
        printf("  Median: %.1f ns  Mean: %.1f ns\n\n",
               med, sum / samples.size());
    }

    printf("Results -> results/ctx_switch_bench.csv\n");
    return 0;
}
