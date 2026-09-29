#pragma once
#include "memlab/sched/scheduler.hpp"
#include <deque>
#include <array>
#include <cstdint>

namespace memlab {

/**
 * Multi-Level Feedback Queue (MLFQ)
 *
 * 3 queues (Q0 highest, Q2 lowest):
 *   Q0: quantum = quantum_base       (interactive)
 *   Q1: quantum = quantum_base * 2
 *   Q2: quantum = quantum_base * 4   (CPU-bound)
 *
 * Rules:
 *   - New processes enter Q0.
 *   - If a process uses its full quantum, it is demoted one level.
 *   - If it blocks before using its quantum, it stays at its current level.
 *   - Every boost_interval ticks, ALL processes are moved back to Q0 (starvation fix).
 */
class MLFQ : public IScheduler {
public:
    static constexpr int kQueues = 3;

    explicit MLFQ(uint32_t quantum_base = 4, uint32_t boost_interval = 100)
        : _qbase(quantum_base), _boost_interval(boost_interval) {}

    void add(Process* p) override {
        _level[p->pid] = 0;       // enter at highest priority
        _queues[0].push_back(p);
    }

    Process* pick_next() override {
        for (int q = 0; q < kQueues; ++q) {
            if (!_queues[q].empty()) {
                auto* p = _queues[q].front();
                _queues[q].pop_front();
                return p;
            }
        }
        return nullptr;
    }

    uint32_t quantum_for(const Process* p) const {
        int lvl = level_of(p);
        return _qbase << static_cast<uint32_t>(lvl); // q0=base, q1=2x, q2=4x
    }

    bool should_preempt(const Process* running, uint32_t ran_ticks) const override {
        return ran_ticks >= quantum_for(running);
    }

    /// Call when a process exhausts its quantum (before re-queuing).
    void on_quantum_expired(Process* p) {
        int lvl = level_of(p);
        if (lvl < kQueues - 1) ++_level[p->pid];
        _queues[_level[p->pid]].push_back(p);
    }

    /// Call when a process blocks (IO/page-fault) — stays at current level.
    void on_unblock(Process* p) override {
        _queues[level_of(p)].push_back(p);
    }

    /// Call every tick to perform periodic priority boost.
    void tick(uint32_t current_tick) {
        if (current_tick % _boost_interval != 0 || current_tick == 0) return;
        // Collect all processes from Q1 and Q2, move to Q0
        for (int q = 1; q < kQueues; ++q) {
            for (auto* p : _queues[q]) {
                _level[p->pid] = 0;
                _queues[0].push_back(p);
            }
            _queues[q].clear();
        }
    }

    const char* name() const override { return "MLFQ"; }

private:
    uint32_t _qbase;
    uint32_t _boost_interval;
    std::deque<Process*>            _queues[kQueues];
    std::unordered_map<int, int>    _level;  // pid -> queue level

    int level_of(const Process* p) const {
        auto it = _level.find(p->pid);
        return (it != _level.end()) ? it->second : 0;
    }
};

} // namespace memlab
