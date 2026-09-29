#pragma once
#include "memlab/sched/scheduler.hpp"
#include <queue>
#include <vector>
#include <cstdint>

namespace memlab {

/**
 * Priority Scheduler (non-preemptive) with aging.
 * Aging: every kAgingInterval ticks in Ready state, effective priority
 * is boosted by 1, preventing indefinite starvation.
 */
class PriorityScheduler : public IScheduler {
public:
    static constexpr uint32_t kAgingInterval = 10; // ticks per priority bump

    void add(Process* p) override { _q.push(p); }

    Process* pick_next() override {
        if (_q.empty()) return nullptr;
        auto* p = _q.top(); _q.pop();
        return p;
    }

    bool should_preempt(const Process*, uint32_t) const override { return false; }

    /// Call every tick for processes in Ready state to age them.
    void age_ready_queue(uint32_t current_tick) {
        // Rebuild heap after aging (simple O(n log n) — acceptable for sim)
        std::vector<Process*> tmp;
        while (!_q.empty()) { tmp.push_back(_q.top()); _q.pop(); }
        for (auto* p : tmp) {
            uint32_t waited = current_tick - p->arrival;
            // Effective priority boost: one level per kAgingInterval ticks
            _aged[p->pid] = waited / kAgingInterval;
            _q.push(p);
        }
    }

    const char* name() const override { return "Priority"; }

private:
    std::unordered_map<int, uint32_t> _aged; // pid -> aging bonus

    struct Cmp {
        const std::unordered_map<int, uint32_t>* aged;
        bool operator()(const Process* a, const Process* b) const {
            int eff_a = static_cast<int>(a->priority) - static_cast<int>((*aged).count(a->pid) ? (*aged).at(a->pid) : 0);
            int eff_b = static_cast<int>(b->priority) - static_cast<int>((*aged).count(b->pid) ? (*aged).at(b->pid) : 0);
            return eff_a > eff_b; // min-heap (lower priority value = higher priority)
        }
    };

    struct AgedPQ {
        std::unordered_map<int, uint32_t>* aged_ref;
        std::vector<Process*> data;

        void push(Process* p) {
            data.push_back(p);
            std::push_heap(data.begin(), data.end(), [&](const Process* a, const Process* b){
                auto ag = [&](const Process* x) {
                    auto it = aged_ref->find(x->pid);
                    int bonus = (it != aged_ref->end()) ? static_cast<int>(it->second) : 0;
                    return static_cast<int>(x->priority) - bonus;
                };
                return ag(a) > ag(b);
            });
        }
        bool  empty() const { return data.empty(); }
        Process* top() const { return data.front(); }
        void pop() {
            std::pop_heap(data.begin(), data.end(), [&](const Process* a, const Process* b){
                auto ag = [&](const Process* x) {
                    auto it = aged_ref->find(x->pid);
                    int bonus = (it != aged_ref->end()) ? static_cast<int>(it->second) : 0;
                    return static_cast<int>(x->priority) - bonus;
                };
                return ag(a) > ag(b);
            });
            data.pop_back();
        }
    } _q{&_aged, {}};
};

} // namespace memlab
