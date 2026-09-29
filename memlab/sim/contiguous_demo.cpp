/**
 * contiguous_demo.cpp
 * Generates contiguous_events.json for the Buddy/First-Fit Allocator visualizer.
 * Self-contained, C++11 compatible.
 */
#include "memlab/sched/process.hpp"
#include "memlab/sched/policies/rr.hpp"
#include "memlab/sim/trace_writer.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <map>
#include <algorithm>
using namespace memlab;

struct Block { size_t offset, size; bool free; };

class FirstFitAllocator {
    std::vector<Block> blocks;
public:
    FirstFitAllocator(size_t total) { blocks.push_back({0, total, true}); }

    void* Allocate(size_t size) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].free && blocks[i].size >= size) {
                if (blocks[i].size > size) {
                    Block rem = {blocks[i].offset + size, blocks[i].size - size, true};
                    blocks[i].size = size; blocks[i].free = false;
                    blocks.insert(blocks.begin() + i + 1, rem);
                } else { blocks[i].free = false; }
                return reinterpret_cast<void*>(blocks[i].offset + 1);
            }
        }
        return nullptr;
    }

    void Free(void* ptr) {
        if (!ptr) return;
        size_t offset = reinterpret_cast<size_t>(ptr) - 1;
        for (auto& b : blocks) if (b.offset == offset) { b.free = true; break; }
        for (size_t i = 0; i + 1 < blocks.size(); ) {
            if (blocks[i].free && blocks[i+1].free) {
                blocks[i].size += blocks[i+1].size;
                blocks.erase(blocks.begin() + i + 1);
            } else ++i;
        }
    }
};

int main() {
    constexpr size_t TOTAL = 8192;
    FirstFitAllocator alloc(TOTAL);
    TraceWriter trace("frontend/contiguous_events.json");
    RoundRobin sched(4);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> arrDist(0, 5);
    std::uniform_int_distribution<int> cpuDist(2, 6);
    std::uniform_int_distribution<int> ioDist(1, 4);

    const int N = 8;
    const char* names[] = {"init","shell","compiler","browser","logger","db_worker","net_daemon","renderer"};
    const size_t memSizes[] = {128, 256, 512, 1024, 2048};

    std::vector<Process> procs(N);
    for (int i = 0; i < N; ++i) {
        auto& p = procs[i]; p.pid = i+1; p.name = names[i];
        p.arrival = arrDist(rng);
        p.mem_bytes = memSizes[rng() % 5];
        int nb = (rng()%3)+1;
        for (int b = 0; b < nb; ++b) {
            p.bursts.push_back({Burst::Type::CPU, (uint32_t)cpuDist(rng)});
            if (b < nb-1) p.bursts.push_back({Burst::Type::IO, (uint32_t)ioDist(rng)});
        }
        p.burst_remaining = p.bursts.empty() ? 0 : p.bursts[0].ticks;
        p.cur_burst = 0;
    }

    uint32_t tick = 0;
    Process* running = nullptr;
    uint32_t ran = 0;
    std::vector<std::pair<Process*, uint32_t>> io_wait;
    std::vector<Process*> mem_wait;
    std::map<int, void*> allocs;

    auto enqueue = [&](Process* p, bool pre) {
        p->state = State::Ready; sched.add(p);
        if (pre) trace.log(tick, "process_ready", p->pid, {{"reason", TraceWriter::str("preempted")}});
        else trace.log(tick, "process_ready", p->pid);
    };

    auto try_admit = [&](Process* p) {
        void* ptr = alloc.Allocate(p->mem_bytes);
        if (!ptr) return false;
        allocs[p->pid] = ptr;
        size_t off = reinterpret_cast<size_t>(ptr) - 1;
        trace.log(tick, "memory_allocated", p->pid, {{"offset", TraceWriter::num(off)}, {"size", TraceWriter::num(p->mem_bytes)}});
        enqueue(p, false);
        return true;
    };

    auto all_done = [&]() { for (auto& p : procs) if (p.state != State::Terminated) return false; return true; };

    while (!all_done() && tick < 1000) {
        for (auto& p : procs) {
            if (p.state == State::New && p.arrival == tick) {
                trace.log(tick, "process_created", p.pid, {{"name", TraceWriter::str(p.name)}, {"memoryRequest", TraceWriter::num(p.mem_bytes)}});
                if (!try_admit(&p)) mem_wait.push_back(&p);
            }
        }
        for (auto it = io_wait.begin(); it != io_wait.end(); ) {
            if (--it->second == 0) {
                auto* p = it->first; trace.log(tick, "io_ack", p->pid);
                it = io_wait.erase(it);
                if (p->advance_burst()) { p->burst_remaining = p->current_burst()->ticks; enqueue(p, false); }
                else {
                    alloc.Free(allocs[p->pid]);
                    trace.log(tick, "memory_freed", p->pid, {{"offset", TraceWriter::num(reinterpret_cast<size_t>(allocs[p->pid])-1)}, {"size", TraceWriter::num(p->mem_bytes)}});
                    trace.log(tick, "process_terminated", p->pid); p->state = State::Terminated;
                }
            } else ++it;
        }
        for (auto it = mem_wait.begin(); it != mem_wait.end(); )
            it = try_admit(*it) ? mem_wait.erase(it) : ++it;
        if (running && sched.should_preempt(running, ran)) { enqueue(running, true); running = nullptr; ran = 0; }
        if (!running) { running = sched.pick_next(); if (running) { ran = 0; trace.log(tick, "process_scheduled", running->pid, {{"burstIndex", TraceWriter::num(running->cur_burst)}}); } }
        if (running) {
            uint32_t rf = std::min(running->burst_remaining, sched.quantum()-ran);
            trace.log(tick, "cpu_run", running->pid, {{"ticks", TraceWriter::num(rf)}, {"remaining", TraceWriter::num(running->burst_remaining-1)}});
            --running->burst_remaining; ++ran;
            if (running->burst_remaining == 0) {
                if (running->advance_burst()) {
                    Burst* nb2 = running->current_burst(); running->burst_remaining = nb2->ticks;
                    if (nb2->type == Burst::Type::IO) {
                        trace.log(tick+1, "io_requested", running->pid, {{"ioTicks", TraceWriter::num(nb2->ticks)}});
                        running->state = State::Waiting; io_wait.push_back({running, nb2->ticks});
                        running = nullptr; ran = 0;
                    }
                } else {
                    alloc.Free(allocs[running->pid]);
                    trace.log(tick+1, "memory_freed", running->pid, {{"offset", TraceWriter::num(reinterpret_cast<size_t>(allocs[running->pid])-1)}, {"size", TraceWriter::num(running->mem_bytes)}});
                    trace.log(tick+1, "process_terminated", running->pid);
                    running->state = State::Terminated; running = nullptr; ran = 0;
                }
            }
        }
        ++tick;
    }
    std::cout << "Wrote contiguous_events.json" << std::endl;
    return 0;
}
