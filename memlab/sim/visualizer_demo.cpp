/**
 * visualizer_demo.cpp
 *
 * Runs the memlab virtual memory simulator (Page Tables, TLB, Process Segments,
 * Page Faults, and Physical Frame Allocation) along with Round-Robin scheduling
 * to generate events.json for the memlab frontend visualizer.
 * 
 * Compatible with C++11 and standard MinGW / GCC compilers.
 */

#include "memlab/sched/process.hpp"
#include "memlab/sched/policies/rr.hpp"
#include "memlab/sim/trace_writer.hpp"

#include <iostream>
#include <random>
#include <vector>
#include <map>
#include <algorithm>
#include <queue>
#include <set>

using namespace memlab;

struct SegmentInfo {
    std::string name;
    uint32_t base_vpn;
    uint32_t num_pages;
};

struct TLBEntry {
    int pid;
    uint32_t vpn;
    uint32_t pfn;
    std::string segment;
    bool valid;
};

struct PageTableEntry {
    uint32_t pfn;
    bool valid;
    std::string segment;
};

class VirtualMemorySystem {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr size_t NUM_PHYSICAL_FRAMES = 32; // 128 KB physical RAM
    static constexpr size_t TLB_SIZE = 8;

    // Physical memory frame tracking
    std::vector<int> frame_owner; // pfn -> pid (-1 if free)
    std::vector<uint32_t> frame_vpn; // pfn -> vpn
    std::vector<std::string> frame_segment; // pfn -> segment name
    std::queue<uint32_t> fifo_frames; // Frame allocation order for replacement

    // Page table: (pid, vpn) -> PageTableEntry
    std::map<std::pair<int, uint32_t>, PageTableEntry> page_table;

    // TLB: fixed size array
    std::vector<TLBEntry> tlb;

    VirtualMemorySystem() {
        frame_owner.assign(NUM_PHYSICAL_FRAMES, -1);
        frame_vpn.assign(NUM_PHYSICAL_FRAMES, 0);
        frame_segment.assign(NUM_PHYSICAL_FRAMES, "");
        tlb.resize(TLB_SIZE, { -1, 0, 0, "", false });
    }

    int find_free_frame() {
        for (size_t i = 0; i < NUM_PHYSICAL_FRAMES; ++i) {
            if (frame_owner[i] == -1) return static_cast<int>(i);
        }
        return -1;
    }

    bool lookup_tlb(int pid, uint32_t vpn, uint32_t& out_pfn) {
        for (const auto& entry : tlb) {
            if (entry.valid && entry.pid == pid && entry.vpn == vpn) {
                out_pfn = entry.pfn;
                return true;
            }
        }
        return false;
    }

    void update_tlb(int pid, uint32_t vpn, uint32_t pfn, const std::string& segment) {
        // Simple FIFO / random TLB replacement
        static size_t tlb_idx = 0;
        tlb[tlb_idx] = { pid, vpn, pfn, segment, true };
        tlb_idx = (tlb_idx + 1) % TLB_SIZE;
    }

    void invalidate_tlb_for_pid(int pid) {
        for (auto& entry : tlb) {
            if (entry.pid == pid) entry.valid = false;
        }
    }

    void invalidate_tlb_for_pfn(uint32_t pfn) {
        for (auto& entry : tlb) {
            if (entry.pfn == pfn) entry.valid = false;
        }
    }
};

int main() {
    TraceWriter trace("frontend/events.json");
    VirtualMemorySystem vm;
    RoundRobin sched(4); // quantum = 4 ticks

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> arrivalDist(0, 8);
    std::uniform_int_distribution<int> cpuBurstDist(3, 7);
    std::uniform_int_distribution<int> ioBurstDist(2, 5);

    const int NUM_PROCESSES = 8;
    const char* names[] = { "init", "shell", "compiler", "browser", "logger",
                           "db_worker", "net_daemon", "renderer" };

    std::vector<Process> procs(NUM_PROCESSES);
    std::map<int, std::vector<SegmentInfo>> proc_segments;

    for (int i = 0; i < NUM_PROCESSES; ++i) {
        auto& p = procs[i];
        p.pid = i + 1;
        p.name = names[i];
        p.arrival = arrivalDist(rng);

        // Define Virtual Memory Segments for each process
        // Code: VPN 0..1, Data: VPN 2..3, Heap: VPN 4..7, Stack: VPN 8..9
        proc_segments[p.pid] = {
            {"Code", 0, 2},
            {"Data", 2, 2},
            {"Heap", 4, 4},
            {"Stack", 8, 2}
        };
        p.mem_bytes = 10 * VirtualMemorySystem::PAGE_SIZE; // 10 virtual pages = 40 KB

        int numBursts = (rng() % 3) + 2;
        for (int b = 0; b < numBursts; ++b) {
            p.bursts.push_back({ Burst::Type::CPU, static_cast<uint32_t>(cpuBurstDist(rng)) });
            if (b < numBursts - 1) {
                p.bursts.push_back({ Burst::Type::IO, static_cast<uint32_t>(ioBurstDist(rng)) });
            }
        }
        p.burst_remaining = p.bursts.empty() ? 0 : p.bursts[0].ticks;
        p.cur_burst = 0;
    }

    uint32_t tick = 0;
    Process* running = nullptr;
    uint32_t ran_ticks = 0;

    std::vector<std::pair<Process*, uint32_t>> io_waiting;

    auto do_enqueue = [&](Process* p, bool preempted) {
        p->state = State::Ready;
        sched.add(p);
        if (preempted) {
            trace.log(tick, "process_ready", p->pid, { {"reason", TraceWriter::str("preempted")} });
        } else {
            trace.log(tick, "process_ready", p->pid);
        }
    };

    auto all_done = [&]() {
        for (const auto& p : procs)
            if (p.state != State::Terminated) return false;
        return true;
    };

    while (!all_done() && tick < 1000) {
        // 1. Process Arrivals
        for (auto& p : procs) {
            if (p.state == State::New && p.arrival == tick) {
                trace.log(tick, "process_created", p.pid, {
                    {"name", TraceWriter::str(p.name)},
                    {"memoryRequest", TraceWriter::num(p.mem_bytes)},
                    {"numPages", TraceWriter::num(10)}
                });

                // Log Virtual Segments initialization
                for (const auto& seg : proc_segments[p.pid]) {
                    trace.log(tick, "segment_created", p.pid, {
                        {"segment", TraceWriter::str(seg.name)},
                        {"base_vpn", TraceWriter::num(seg.base_vpn)},
                        {"num_pages", TraceWriter::num(seg.num_pages)}
                    });
                }

                do_enqueue(&p, false);
            }
        }

        // 2. I/O Completions
        for (auto it = io_waiting.begin(); it != io_waiting.end(); ) {
            if (--it->second == 0) {
                auto* p = it->first;
                trace.log(tick, "io_ack", p->pid);
                it = io_waiting.erase(it);
                if (p->advance_burst()) {
                    p->burst_remaining = p->current_burst()->ticks;
                    do_enqueue(p, false);
                } else {
                    // Free virtual memory & physical frames
                    for (uint32_t pfn = 0; pfn < VirtualMemorySystem::NUM_PHYSICAL_FRAMES; ++pfn) {
                        if (vm.frame_owner[pfn] == p->pid) {
                            vm.frame_owner[pfn] = -1;
                            trace.log(tick, "page_freed", p->pid, {
                                {"pfn", TraceWriter::num(pfn)}
                            });
                        }
                    }
                    vm.invalidate_tlb_for_pid(p->pid);
                    trace.log(tick, "process_terminated", p->pid);
                    p->state = State::Terminated;
                }
            } else {
                ++it;
            }
        }

        // 3. Preemption
        if (running && sched.should_preempt(running, ran_ticks)) {
            do_enqueue(running, true);
            running = nullptr;
            ran_ticks = 0;
        }

        // 4. Schedule
        if (!running) {
            running = sched.pick_next();
            if (running) {
                ran_ticks = 0;
                trace.log(tick, "process_scheduled", running->pid, {
                    {"burstIndex", TraceWriter::num(running->cur_burst)}
                });
            }
        }

        // 5. Memory Access & Execution Simulation
        if (running) {
            // Pick a virtual page to access based on current execution (Code -> Data -> Heap -> Stack)
            const auto& segs = proc_segments[running->pid];
            int seg_idx = (tick + running->pid) % segs.size();
            const auto& target_seg = segs[seg_idx];
            uint32_t target_vpn = target_seg.base_vpn + (rng() % target_seg.num_pages);
            uint32_t target_vaddr = target_vpn * VirtualMemorySystem::PAGE_SIZE + (rng() % 1024);

            uint32_t pfn = 0;
            bool hit = vm.lookup_tlb(running->pid, target_vpn, pfn);

            if (hit) {
                // TLB Hit
                trace.log(tick, "tlb_hit", running->pid, {
                    {"vpn", TraceWriter::num(target_vpn)},
                    {"pfn", TraceWriter::num(pfn)},
                    {"segment", TraceWriter::str(target_seg.name)},
                    {"vaddr", TraceWriter::num(target_vaddr)}
                });
            } else {
                // TLB Miss -> Check Page Table
                trace.log(tick, "tlb_miss", running->pid, {
                    {"vpn", TraceWriter::num(target_vpn)},
                    {"segment", TraceWriter::str(target_seg.name)}
                });

                auto pte_it = vm.page_table.find({ running->pid, target_vpn });
                if (pte_it != vm.page_table.end() && pte_it->second.valid) {
                    pfn = pte_it->second.pfn;
                    vm.update_tlb(running->pid, target_vpn, pfn, target_seg.name);
                    trace.log(tick, "page_table_hit", running->pid, {
                        {"vpn", TraceWriter::num(target_vpn)},
                        {"pfn", TraceWriter::num(pfn)}
                    });
                } else {
                    // Page Fault! Demand paging allocation
                    trace.log(tick, "page_fault", running->pid, {
                        {"vpn", TraceWriter::num(target_vpn)},
                        {"segment", TraceWriter::str(target_seg.name)},
                        {"vaddr", TraceWriter::num(target_vaddr)}
                    });

                    int free_pfn = vm.find_free_frame();
                    if (free_pfn == -1) {
                        // Page Replacement (FIFO eviction)
                        free_pfn = vm.fifo_frames.front();
                        vm.fifo_frames.pop();
                        int evicted_pid = vm.frame_owner[free_pfn];
                        uint32_t evicted_vpn = vm.frame_vpn[free_pfn];

                        vm.page_table[{evicted_pid, evicted_vpn}].valid = false;
                        vm.invalidate_tlb_for_pfn(free_pfn);

                        trace.log(tick, "page_evicted", running->pid, {
                            {"evicted_pid", TraceWriter::num(evicted_pid)},
                            {"evicted_vpn", TraceWriter::num(evicted_vpn)},
                            {"pfn", TraceWriter::num(free_pfn)}
                        });
                    }

                    pfn = static_cast<uint32_t>(free_pfn);
                    vm.frame_owner[pfn] = running->pid;
                    vm.frame_vpn[pfn] = target_vpn;
                    vm.frame_segment[pfn] = target_seg.name;
                    vm.fifo_frames.push(pfn);

                    vm.page_table[{running->pid, target_vpn}] = { pfn, true, target_seg.name };
                    vm.update_tlb(running->pid, target_vpn, pfn, target_seg.name);

                    trace.log(tick, "page_allocated", running->pid, {
                        {"vpn", TraceWriter::num(target_vpn)},
                        {"pfn", TraceWriter::num(pfn)},
                        {"segment", TraceWriter::str(target_seg.name)}
                    });
                }
            }

            // Execute CPU step
            uint32_t run_for = std::min(running->burst_remaining, sched.quantum() - ran_ticks);
            trace.log(tick, "cpu_run", running->pid, {
                {"ticks", TraceWriter::num(run_for)},
                {"remaining", TraceWriter::num(running->burst_remaining - 1)}
            });

            --running->burst_remaining;
            ++ran_ticks;

            if (running->burst_remaining == 0) {
                if (running->advance_burst()) {
                    Burst* nb = running->current_burst();
                    running->burst_remaining = nb->ticks;
                    if (nb->type == Burst::Type::IO) {
                        trace.log(tick + 1, "io_requested", running->pid, {
                            {"ioTicks", TraceWriter::num(nb->ticks)}
                        });
                        running->state = State::Waiting;
                        io_waiting.push_back({ running, nb->ticks });
                        running = nullptr;
                        ran_ticks = 0;
                    }
                } else {
                    // Free frames on process termination
                    for (uint32_t f = 0; f < VirtualMemorySystem::NUM_PHYSICAL_FRAMES; ++f) {
                        if (vm.frame_owner[f] == running->pid) {
                            vm.frame_owner[f] = -1;
                            trace.log(tick + 1, "page_freed", running->pid, {
                                {"pfn", TraceWriter::num(f)}
                            });
                        }
                    }
                    vm.invalidate_tlb_for_pid(running->pid);
                    trace.log(tick + 1, "process_terminated", running->pid);
                    running->state = State::Terminated;
                    running = nullptr;
                    ran_ticks = 0;
                }
            }
        }

        ++tick;
    }

    std::cout << "Wrote Memlab Virtual Memory & Page Table simulation trace to frontend/events.json" << std::endl;
    return 0;
}
