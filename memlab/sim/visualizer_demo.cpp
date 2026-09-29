/**
 * visualizer_demo.cpp
 *
 * Runs the memlab scheduler and a simple mock allocator together to produce
 * events.json for the frontend visualizer.
 * 
 * Note: Uses a self-contained First-Fit allocator to avoid dependencies on
 * older C++14/17 code in the root directory that may fail to compile on GCC 6.3.
 */

#include "memlab/sched/process.hpp"
#include "memlab/sched/policies/rr.hpp"
#include "memlab/sim/trace_writer.hpp"

#include <iostream>
#include <random>
#include <vector>
#include <map>
#include <algorithm>

using namespace memlab;

// A simple First-Fit allocator for the visualization demo
struct Block {
    size_t offset;
    size_t size;
    bool free;
};

class SimpleAllocator {
    std::vector<Block> blocks;
public:
    SimpleAllocator(size_t total) {
        blocks.push_back({0, total, true});
    }
    
    void* Allocate(size_t size) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].free && blocks[i].size >= size) {
                if (blocks[i].size > size) {
                    Block remaining = {blocks[i].offset + size, blocks[i].size - size, true};
                    blocks[i].size = size;
                    blocks[i].free = false;
                    blocks.insert(blocks.begin() + i + 1, remaining);
                } else {
                    blocks[i].free = false;
                }
                return reinterpret_cast<void*>(blocks[i].offset + 1); // +1 so it's non-null
            }
        }
        return nullptr;
    }
    
    void Free(void* ptr) {
        if (!ptr) return;
        size_t offset = reinterpret_cast<size_t>(ptr) - 1;
        for (auto& b : blocks) {
            if (b.offset == offset) {
                b.free = true;
                break;
            }
        }
        // Coalesce free blocks
        for (size_t i = 0; i + 1 < blocks.size(); ) {
            if (blocks[i].free && blocks[i+1].free) {
                blocks[i].size += blocks[i+1].size;
                blocks.erase(blocks.begin() + i + 1);
            } else {
                ++i;
            }
        }
    }
};


int main() {
    constexpr size_t TOTAL_MEMORY = 8192;

    SimpleAllocator allocator(TOTAL_MEMORY);
    TraceWriter trace("frontend/events.json"); // output to frontend dir
    
    // Using memlab's Round Robin scheduler
    RoundRobin sched(4); // quantum = 4

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> arrivalDist(0, 6);
    std::uniform_int_distribution<int> cpuBurstDist(2, 6);
    std::uniform_int_distribution<int> ioBurstDist(1, 4);

    const int NUM_PROCESSES = 8;
    const char* names[] = {"init", "shell", "compiler", "browser", "logger",
                           "db_worker", "net_daemon", "renderer"};
    const size_t memSizes[] = {128, 256, 512, 1024, 2048};

    std::vector<Process> procs(NUM_PROCESSES);
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        auto& p = procs[i];
        p.pid = i + 1;
        p.name = names[i];
        p.arrival = arrivalDist(rng);
        p.mem_bytes = memSizes[rng() % 5];
        
        int numBursts = (rng() % 3) + 1;
        for (int b = 0; b < numBursts; ++b) {
            p.bursts.push_back({Burst::Type::CPU, static_cast<uint32_t>(cpuBurstDist(rng))});
            if (b < numBursts - 1) {
                p.bursts.push_back({Burst::Type::IO, static_cast<uint32_t>(ioBurstDist(rng))});
            }
        }
        p.burst_remaining = p.bursts.empty() ? 0 : p.bursts[0].ticks;
        p.cur_burst = 0;
    }

    uint32_t tick = 0;
    Process* running = nullptr;
    uint32_t ran_ticks = 0;
    
    std::vector<std::pair<Process*, uint32_t>> io_waiting;
    std::vector<Process*> memory_waiting; // pending admission
    std::map<int, void*> allocations;     // pid -> pointer

    // Create a local lambda object we can pass around
    auto do_enqueue = [&](Process* p, bool preempted) {
        p->state = State::Ready;
        sched.add(p);
        if (preempted) {
            trace.log(tick, "process_ready", p->pid, {{"reason", TraceWriter::str("preempted")}});
        } else {
            trace.log(tick, "process_ready", p->pid);
        }
    };

    auto try_admit = [&](Process* p) {
        void* ptr = allocator.Allocate(p->mem_bytes);
        if (ptr) {
            allocations[p->pid] = ptr;
            size_t offset = reinterpret_cast<size_t>(ptr) - 1;
            trace.log(tick, "memory_allocated", p->pid, {
                {"offset", TraceWriter::num(offset)},
                {"size", TraceWriter::num(p->mem_bytes)}
            });
            do_enqueue(p, false);
            return true;
        }
        return false; // admission delayed due to memory
    };

    auto all_done = [&]() {
        for (const auto& p : procs)
            if (p.state != State::Terminated) return false;
        return true;
    };

    while (!all_done() && tick < 1000) {
        // 1. Arrivals
        for (auto& p : procs) {
            if (p.state == State::New && p.arrival == tick) {
                trace.log(tick, "process_created", p.pid, {
                    {"name", TraceWriter::str(p.name)},
                    {"memoryRequest", TraceWriter::num(p.mem_bytes)}
                });
                if (!try_admit(&p)) {
                    memory_waiting.push_back(&p);
                }
            }
        }

        // 2. I/O completions
        for (auto it = io_waiting.begin(); it != io_waiting.end(); ) {
            if (--it->second == 0) {
                auto* p = it->first;
                trace.log(tick, "io_ack", p->pid);
                it = io_waiting.erase(it);
                if (p->advance_burst()) {
                    p->burst_remaining = p->current_burst()->ticks;
                    do_enqueue(p, false);
                } else {
                    allocator.Free(allocations[p->pid]);
                    trace.log(tick, "memory_freed", p->pid, {
                        {"offset", TraceWriter::num(reinterpret_cast<size_t>(allocations[p->pid]) - 1)},
                        {"size", TraceWriter::num(p->mem_bytes)}
                    });
                    trace.log(tick, "process_terminated", p->pid);
                    p->state = State::Terminated;
                }
            } else {
                ++it;
            }
        }

        // 3. Retry admission for memory-waiting processes
        for (auto it = memory_waiting.begin(); it != memory_waiting.end(); ) {
            if (try_admit(*it)) {
                it = memory_waiting.erase(it);
            } else {
                ++it;
            }
        }

        // 4. Preemption
        if (running && sched.should_preempt(running, ran_ticks)) {
            do_enqueue(running, true);
            running = nullptr;
            ran_ticks = 0;
        }

        // 5. Schedule
        if (!running) {
            running = sched.pick_next();
            if (running) {
                ran_ticks = 0;
                trace.log(tick, "process_scheduled", running->pid, {
                    {"burstIndex", TraceWriter::num(running->cur_burst)}
                });
            }
        }

        // 6. Run
        if (running) {
            uint32_t run_for = std::min(running->burst_remaining, sched.quantum() - ran_ticks);
            trace.log(tick, "cpu_run", running->pid, {
                {"ticks", TraceWriter::num(run_for)},
                {"remaining", TraceWriter::num(running->burst_remaining - run_for)}
            });
            
            // Advance by 1 tick
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
                        io_waiting.push_back({running, nb->ticks});
                        running = nullptr;
                        ran_ticks = 0;
                    }
                } else {
                    allocator.Free(allocations[running->pid]);
                    trace.log(tick + 1, "memory_freed", running->pid, {
                        {"offset", TraceWriter::num(reinterpret_cast<size_t>(allocations[running->pid]) - 1)},
                        {"size", TraceWriter::num(running->mem_bytes)}
                    });
                    trace.log(tick + 1, "process_terminated", running->pid);
                    running->state = State::Terminated;
                    running = nullptr;
                    ran_ticks = 0;
                }
            }
        }

        ++tick;
    }

    std::cout << "Wrote NEW trace to frontend/events.json" << std::endl;
    return 0;
}
