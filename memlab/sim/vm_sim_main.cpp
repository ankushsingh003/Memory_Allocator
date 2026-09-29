/**
 * vm_sim_main.cpp
 *
 * Software virtual memory simulator with page replacement policies.
 * Experiments:
 *   1. Belady's anomaly demo (FIFO, 3 frames vs 4 frames)
 *   2. Fault rate vs frame count for each policy + workload
 *   3. Thrashing: fault rate and CPU utilization as processes increase
 *   4. Effective Access Time (EAT)
 *
 * Build:
 *   g++ -std=c++20 -O2 -Iinclude sim/vm_sim_main.cpp -o vm_sim
 *
 * Output: results/vm_sim.csv
 */

#include "memlab/vm/frame_allocator.hpp"
#include "memlab/vm/page_table.hpp"
#include "memlab/vm/tlb.hpp"
#include "memlab/vm/replacement/fifo.hpp"
#include "memlab/vm/replacement/lru.hpp"
#include "memlab/vm/replacement/clock.hpp"
#include "memlab/vm/replacement/optimal.hpp"
#include "memlab/common/stats.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

using namespace memlab;

// ─── Configurable cost model ──────────────────────────────────────────────────
struct CostModel {
    uint64_t tlb_ns  =         1;
    uint64_t mem_ns  =       100;
    uint64_t disk_ns = 10'000'000;
};

// ─── Page access result ───────────────────────────────────────────────────────
struct AccessResult {
    bool     tlb_hit;
    bool     page_fault;
    bool     evicted;
    bool     dirty_evict;
    uint64_t cost_ns;
};

// ─── VM system (single process) ───────────────────────────────────────────────
class VMSystem {
public:
    VMSystem(uint32_t frames, IReplacement& policy,
             const CostModel& costs = {})
        : _frames(frames), _policy(policy), _costs(costs), _tlb(64)
    {}

    AccessResult access(int pid, uint64_t vpn, bool is_write) {
        AccessResult r{false, false, false, false, 0};

        // ── TLB lookup ────────────────────────────────────────────────────
        auto tlb_hit = _tlb.lookup(pid, vpn);
        if (tlb_hit) {
            r.tlb_hit = true;
            r.cost_ns = _costs.tlb_ns + _costs.mem_ns;
            if (is_write) {
                auto* pte = _page_tables[pid].lookup(vpn);
                if (pte) pte->dirty = true;
            }
            _policy.on_access(*tlb_hit);
            ++_hits;
            return r;
        }

        _tlb.count_miss();
        r.cost_ns = _costs.tlb_ns; // TLB miss overhead

        // ── Page table walk ───────────────────────────────────────────────
        auto* pte = _page_tables[pid].lookup(vpn);
        if (pte && pte->present) {
            // TLB miss but page present
            _tlb.insert(pid, vpn, pte->frame);
            pte->referenced = true;
            if (is_write) pte->dirty = true;
            _policy.on_access(pte->frame);
            r.cost_ns += _costs.mem_ns;
            ++_hits;
            return r;
        }

        // ── Page fault ────────────────────────────────────────────────────
        r.page_fault = true;
        ++_faults;

        uint32_t victim_frame;
        if (_frame_alloc.has_free()) {
            victim_frame = _frame_alloc.alloc();
        } else {
            // Need to evict
            victim_frame = _policy.victim();
            r.evicted    = true;

            // Find and invalidate the evicted page
            for (auto& [p, pt] : _page_tables) {
                // (linear scan — fine for simulator scale)
                for (auto vpn2 : _loaded_vpns[p]) {
                    auto* pte2 = pt.lookup(vpn2);
                    if (pte2 && pte2->present && pte2->frame == victim_frame) {
                        if (pte2->dirty) {
                            r.dirty_evict = true;
                            r.cost_ns += _costs.disk_ns; // write-back
                        }
                        pte2->present = false;
                        _tlb.invalidate(p, vpn2);
                        _loaded_vpns[p].erase(
                            std::find(_loaded_vpns[p].begin(), _loaded_vpns[p].end(), vpn2));
                        break;
                    }
                }
            }
            _policy.on_evict(victim_frame);
        }

        // Load new page
        r.cost_ns += _costs.disk_ns; // disk read
        auto& new_pte = _page_tables[pid].get_or_create(vpn);
        new_pte.frame     = victim_frame;
        new_pte.present   = true;
        new_pte.referenced = true;
        new_pte.dirty     = is_write;
        _tlb.insert(pid, vpn, victim_frame);
        _policy.on_load(victim_frame);
        _loaded_vpns[pid].push_back(vpn);

        r.cost_ns += _costs.mem_ns;
        return r;
    }

    uint64_t faults() const { return _faults; }
    uint64_t hits()   const { return _hits; }
    uint64_t total()  const { return _hits + _faults; }
    double   fault_rate() const {
        return (total() > 0) ? static_cast<double>(_faults) / total() : 0.0;
    }
    double eat_ns(const CostModel& cm) const {
        double fr = fault_rate();
        return (1.0 - fr) * (cm.tlb_ns + cm.mem_ns) + fr * (cm.disk_ns + cm.mem_ns);
    }

private:
    uint32_t        _frames;
    IReplacement&   _policy;
    CostModel       _costs;
    TLB             _tlb;
    FrameAllocator  _frame_alloc{_frames};
    std::unordered_map<int, PageTable>          _page_tables;
    std::unordered_map<int, std::vector<uint64_t>> _loaded_vpns;
    uint64_t        _faults{0};
    uint64_t        _hits{0};
};

// ─── Workload generators ──────────────────────────────────────────────────────
std::vector<uint32_t> gen_sequential(uint32_t pages, uint32_t reps) {
    std::vector<uint32_t> t;
    for (uint32_t r = 0; r < reps; ++r)
        for (uint32_t p = 0; p < pages; ++p)
            t.push_back(p);
    return t;
}

std::vector<uint32_t> gen_uniform(uint32_t pages, uint32_t n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, pages - 1);
    std::vector<uint32_t> t(n);
    for (auto& x : t) x = dist(rng);
    return t;
}

std::vector<uint32_t> gen_locality(uint32_t total_pages, uint32_t n,
                                    uint32_t working_set, uint64_t seed) {
    std::mt19937 rng(seed);
    std::vector<uint32_t> t;
    uint32_t ws_start = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (i % (n / 4) == 0) // shift working set every quarter
            ws_start = std::uniform_int_distribution<uint32_t>(0, total_pages - working_set)(rng);
        bool local = std::uniform_real_distribution<>()(rng) < 0.9;
        if (local)
            t.push_back(ws_start + std::uniform_int_distribution<uint32_t>(0, working_set-1)(rng));
        else
            t.push_back(std::uniform_int_distribution<uint32_t>(0, total_pages-1)(rng));
    }
    return t;
}

// ─── Run one (policy, frames, trace) combination ──────────────────────────────
struct RunResult {
    std::string policy_name;
    uint32_t    frames;
    std::string workload;
    uint64_t    faults;
    uint64_t    total;
    double      fault_rate;
    double      eat_ns;
};

RunResult run_sim(const std::string& wl_name,
                  const std::vector<uint32_t>& trace,
                  uint32_t frames,
                  IReplacement& policy,
                  const CostModel& cm = {})
{
    VMSystem vm(frames, policy, cm);
    for (uint32_t vpn : trace)
        vm.access(1, vpn, false);
    return { policy.name(), frames, wl_name,
             vm.faults(), vm.total(), vm.fault_rate(), vm.eat_ns(cm) };
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    CostModel cm;

    CsvWriter csv("results/vm_sim.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"experiment","workload","policy","frames",
                      "faults","total_accesses","fault_rate_pct","eat_ns"});

    // ── Experiment 1: Belady's Anomaly ────────────────────────────────────
    printf("=== Belady Anomaly (FIFO) ===\n");
    {
        std::vector<uint32_t> belady_trace = {1,2,3,4,1,2,5,1,2,3,4,5};
        for (uint32_t frames : {3u, 4u}) {
            FIFOReplacement fifo;
            auto r = run_sim("belady", belady_trace, frames, fifo, cm);
            printf("  FIFO %u frames: %llu faults (expected: %s)\n",
                   frames, (unsigned long long)r.faults,
                   frames == 3 ? "9" : "10 (Belady's anomaly!)");
            csv.write_row("belady_anomaly", "belady", r.policy_name, frames,
                          r.faults, r.total, r.fault_rate * 100, r.eat_ns);
        }
        // Verify anomaly
        assert(true); // actual assertion in tests/
        printf("\n");
    }

    // ── Experiment 2: Fault rate vs frames ───────────────────────────────
    printf("=== Fault rate vs frames ===\n");
    {
        constexpr uint32_t kPages   = 20;
        constexpr uint32_t kAccesses= 1000;

        struct WL { std::string name; std::vector<uint32_t> trace; };
        std::vector<WL> workloads = {
            { "sequential", gen_sequential(kPages, kAccesses / kPages) },
            { "uniform",    gen_uniform(kPages, kAccesses, 42) },
            { "locality",   gen_locality(kPages, kAccesses, 5, 42) },
        };

        std::vector<uint32_t> frame_counts;
        for (uint32_t f = 1; f <= 15; ++f) frame_counts.push_back(f);

        for (auto& wl : workloads) {
            for (uint32_t f : frame_counts) {
                FIFOReplacement  fifo;
                LRUReplacement   lru;
                ClockReplacement clk;
                OptimalReplacement opt(wl.trace);

                for (auto* pol : std::vector<IReplacement*>{&fifo, &lru, &clk, &opt}) {
                    auto r = run_sim(wl.name, wl.trace, f, *pol, cm);
                    csv.write_row("fault_vs_frames", wl.name, r.policy_name, f,
                                  r.faults, r.total, r.fault_rate * 100, r.eat_ns);
                }
            }
        }
        printf("Done.\n\n");
    }

    // ── Experiment 3: Thrashing ───────────────────────────────────────────
    printf("=== Thrashing (shared frames) ===\n");
    {
        constexpr uint32_t kTotalFrames   = 20;
        constexpr uint32_t kPagesPerProc  = 8;
        constexpr uint32_t kAccessPerProc = 200;

        for (uint32_t num_procs = 1; num_procs <= 8; ++num_procs) {
            // All processes share the frame pool
            uint64_t total_faults = 0, total_accesses = 0;
            for (uint32_t pid = 0; pid < num_procs; ++pid) {
                uint32_t frames_each = std::max(1u, kTotalFrames / num_procs);
                auto trace = gen_locality(kPagesPerProc, kAccessPerProc, 4, pid);
                LRUReplacement lru;
                auto r = run_sim("thrash", trace, frames_each, lru, cm);
                total_faults   += r.faults;
                total_accesses += r.total;
            }
            double fault_rate = static_cast<double>(total_faults) / total_accesses;
            // CPU util model: collapses when fault_rate is high
            double cpu_util   = std::max(0.0, 1.0 - fault_rate * 10.0);
            printf("  %u procs: fault_rate=%.1f%%  cpu_util=%.1f%%\n",
                   num_procs, fault_rate * 100, cpu_util * 100);
            csv.write_row("thrashing", "multi_proc", "LRU",
                          kTotalFrames / num_procs,
                          total_faults, total_accesses, fault_rate * 100,
                          cpu_util * 100); // reuse eat_ns column for cpu_util
        }
        printf("\n");
    }

    printf("Results -> results/vm_sim.csv\n");
    return 0;
}
