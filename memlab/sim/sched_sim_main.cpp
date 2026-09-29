/**
 * sched_sim_main.cpp
 *
 * Tick-based CPU scheduler simulator.
 * Sweeps:
 *   1. Quantum (1-50) vs CPU utilization and avg response time (for RR & MLFQ)
 *   2. cs_cost (0, 1, 5, 10) across all policies
 *   3. All policies compared on the same seeded workloads:
 *      CPU-heavy, IO-heavy, mixed
 *
 * Build:
 *   g++ -std=c++20 -O2 -Iinclude sim/sched_sim_main.cpp -o sched_sim -lpthread
 *
 * Output: results/sched_sweep.csv
 */

#include "memlab/sched/process.hpp"
#include "memlab/sched/scheduler.hpp"
#include "memlab/sched/policies/fcfs.hpp"
#include "memlab/sched/policies/sjf.hpp"
#include "memlab/sched/policies/srtf.hpp"
#include "memlab/sched/policies/rr.hpp"
#include "memlab/sched/policies/priority.hpp"
#include "memlab/sched/policies/mlfq.hpp"
#include "memlab/common/stats.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

using namespace memlab;

// ─── Workload type ────────────────────────────────────────────────────────────
enum class WorkloadType { CpuHeavy, IoHeavy, Mixed };

inline const char* wl_name(WorkloadType w) {
    switch (w) {
        case WorkloadType::CpuHeavy: return "cpu_heavy";
        case WorkloadType::IoHeavy:  return "io_heavy";
        case WorkloadType::Mixed:    return "mixed";
    }
    return "?";
}

// ─── Process factory ──────────────────────────────────────────────────────────
std::vector<Process> make_workload(WorkloadType type, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    auto cpu_dist = [&](int lo, int hi) {
        return std::uniform_int_distribution<uint32_t>(lo, hi)(rng);
    };

    std::vector<Process> procs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        auto& p     = procs[static_cast<size_t>(i)];
        p.pid       = i + 1;
        p.name      = "P" + std::to_string(i + 1);
        p.arrival   = cpu_dist(0, 10);
        p.priority  = cpu_dist(1, 5);
        p.mem_bytes = 0;

        int num_bursts = (type == WorkloadType::CpuHeavy) ? 3 :
                         (type == WorkloadType::IoHeavy)  ? 7 : 5;

        for (int b = 0; b < num_bursts; ++b) {
            bool is_cpu = (b % 2 == 0); // alternate start with CPU
            if (type == WorkloadType::CpuHeavy) {
                p.bursts.push_back({ is_cpu ? Burst::Type::CPU : Burst::Type::IO,
                                     is_cpu ? cpu_dist(10, 40) : cpu_dist(2, 8) });
            } else if (type == WorkloadType::IoHeavy) {
                p.bursts.push_back({ is_cpu ? Burst::Type::CPU : Burst::Type::IO,
                                     is_cpu ? cpu_dist(2, 6) : cpu_dist(10, 30) });
            } else {
                p.bursts.push_back({ is_cpu ? Burst::Type::CPU : Burst::Type::IO,
                                     cpu_dist(5, 20) });
            }
        }
        p.burst_remaining = p.bursts.empty() ? 0 : p.bursts[0].ticks;
        p.cur_burst       = 0;
    }
    return procs;
}

// ─── Simulation engine ────────────────────────────────────────────────────────
struct SimResult {
    double   cpu_util{0};
    double   avg_turnaround{0};
    double   avg_waiting{0};
    double   avg_response{0};
    uint32_t total_ctx_switches{0};
    uint32_t total_overhead_ticks{0};
    uint32_t total_ticks{0};
};

SimResult simulate(IScheduler& sched,
                   std::vector<Process> procs,      // copy — will be mutated
                   const CsModel& cs,
                   uint32_t quantum = 4)            // used for RR/MLFQ
{
    // Reset all process state
    for (auto& p : procs) {
        p.state         = State::New;
        p.cur_burst     = 0;
        p.burst_remaining = p.bursts.empty() ? 0 : p.bursts[0].ticks;
        p.first_run     = UINT32_MAX;
        p.finish        = 0;
        p.wait_ticks    = 0;
        p.io_ticks      = 0;
        p.ctx_switches  = 0;
    }

    uint32_t tick         = 0;
    uint32_t useful_ticks = 0;
    uint32_t cs_overhead  = 0;
    uint32_t total_cs     = 0;

    Process* running      = nullptr;
    uint32_t ran_ticks    = 0;

    // IO wait queue: {process*, ticks_remaining}
    std::vector<std::pair<Process*, uint32_t>> waiting;

    auto enqueue = [&](Process* p) {
        p->state = State::Ready;
        sched.add(p);
    };

    // Finish condition
    auto all_done = [&]() {
        for (auto& p : procs)
            if (p.state != State::Terminated) return false;
        return true;
    };

    uint32_t max_ticks = 50000; // safety cap

    while (!all_done() && tick < max_ticks) {

        // ── Arrivals ─────────────────────────────────────────────────────
        for (auto& p : procs) {
            if (p.state == State::New && p.arrival == tick) {
                enqueue(&p);
            }
        }

        // ── IO completions ────────────────────────────────────────────────
        for (auto it = waiting.begin(); it != waiting.end(); ) {
            if (--it->second == 0) {
                auto* p = it->first;
                it = waiting.erase(it);
                // Advance past IO burst
                if (p->advance_burst()) {
                    p->burst_remaining = p->current_burst()->ticks;
                    enqueue(p);
                } else {
                    p->state  = State::Terminated;
                    p->finish = tick;
                }
            } else ++it;
        }

        // ── Preemption / quantum check ────────────────────────────────────
        if (running && sched.should_preempt(running, ran_ticks)) {
            enqueue(running);
            running    = nullptr;
            ran_ticks  = 0;
        }

        // ── Context-switch overhead ───────────────────────────────────────
        if (!running) {
            Process* next = sched.pick_next();
            if (next) {
                if (running != next && cs.cost_ticks > 0) {
                    // burn idle ticks
                    tick      += cs.cost_ticks;
                    cs_overhead += cs.cost_ticks;
                    ++total_cs;
                }
                if (next->first_run == UINT32_MAX) next->first_run = tick;
                next->ctx_switches++;
                next->state = State::Running;
                running   = next;
                ran_ticks = 0;
            }
        }

        // ── Run one tick ──────────────────────────────────────────────────
        if (running) {
            ++useful_ticks;
            --running->burst_remaining;
            ++ran_ticks;

            if (running->burst_remaining == 0) {
                // Burst complete
                Burst* b = running->current_burst();
                if (b && b->type == Burst::Type::CPU) {
                    // Move to next burst
                    if (running->advance_burst()) {
                        Burst* nb = running->current_burst();
                        running->burst_remaining = nb->ticks;
                        if (nb->type == Burst::Type::IO) {
                            // Block for IO
                            running->state = State::Waiting;
                            waiting.push_back({running, nb->ticks});
                            running   = nullptr;
                            ran_ticks = 0;
                        }
                    } else {
                        running->state  = State::Terminated;
                        running->finish = tick + 1;
                        running   = nullptr;
                        ran_ticks = 0;
                    }
                }
            }
        }

        // ── Accumulate wait ticks ─────────────────────────────────────────
        // (approximate: count processes in Ready state each tick)
        tick++;
    }

    // ── Compute metrics ───────────────────────────────────────────────────
    SimResult r;
    r.total_ticks       = tick;
    r.total_ctx_switches = total_cs;
    r.total_overhead_ticks = cs_overhead;
    r.cpu_util          = (tick > 0)
        ? static_cast<double>(useful_ticks) / static_cast<double>(tick) : 0;

    uint32_t sum_tt = 0, sum_wt = 0, sum_rt = 0;
    int      done   = 0;
    for (auto& p : procs) {
        if (p.state == State::Terminated) {
            sum_tt += p.turnaround();
            sum_rt += p.response();
            ++done;
        }
    }
    if (done > 0) {
        r.avg_turnaround = static_cast<double>(sum_tt) / done;
        r.avg_response   = static_cast<double>(sum_rt) / done;
    }
    return r;
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    constexpr int   kProcesses = 10;
    constexpr uint64_t kSeed  = 42;

    CsvWriter csv("results/sched_sweep.csv");
    csv.write_row(std::vector<std::string>{machine_info_comment()});
    csv.write_header({"workload","policy","quantum","cs_cost",
                      "cpu_util","avg_turnaround","avg_response",
                      "ctx_switches","overhead_ticks","total_ticks"});

    WorkloadType workloads[] = {
        WorkloadType::CpuHeavy,
        WorkloadType::IoHeavy,
        WorkloadType::Mixed
    };

    // ── Sweep 1: Quantum for RR ───────────────────────────────────────────
    printf("=== Quantum sweep (RR) ===\n");
    {
        auto procs = make_workload(WorkloadType::Mixed, kProcesses, kSeed);
        CsModel cs{0, 0, 1};
        for (uint32_t q = 1; q <= 50; ++q) {
            RoundRobin rr(q);
            auto r = simulate(rr, procs, cs, q);
            csv.write_row("mixed", "RR", q, 0,
                          r.cpu_util, r.avg_turnaround, r.avg_response,
                          r.total_ctx_switches, r.total_overhead_ticks, r.total_ticks);
        }
    }

    // ── Sweep 2: cs_cost across all policies ──────────────────────────────
    printf("=== cs_cost sweep ===\n");
    {
        uint32_t cs_costs[] = {0, 1, 5, 10};
        for (auto wl : workloads) {
            auto procs = make_workload(wl, kProcesses, kSeed);
            for (uint32_t cost : cs_costs) {
                CsModel cs{cost, 0, 1};

                // Run each policy
                auto run_and_write = [&](IScheduler& sched, uint32_t q) {
                    auto r = simulate(sched, procs, cs, q);
                    csv.write_row(wl_name(wl), sched.name(), q, cost,
                                  r.cpu_util, r.avg_turnaround, r.avg_response,
                                  r.total_ctx_switches, r.total_overhead_ticks,
                                  r.total_ticks);
                    printf("  %-10s %-8s cs=%u  util=%.1f%%  resp=%.1f\n",
                           wl_name(wl), sched.name(), cost,
                           r.cpu_util * 100, r.avg_response);
                };

                FCFS          fcfs;
                SJF           sjf;
                SRTF          srtf;
                RoundRobin    rr(4);
                PriorityScheduler pri;
                MLFQ          mlfq(4, 100);

                run_and_write(fcfs, 0);
                run_and_write(sjf,  0);
                run_and_write(srtf, 0);
                run_and_write(rr,   4);
                run_and_write(pri,  0);
                run_and_write(mlfq, 4);
            }
        }
    }

    printf("\nResults -> results/sched_sweep.csv\n");
    return 0;
}
