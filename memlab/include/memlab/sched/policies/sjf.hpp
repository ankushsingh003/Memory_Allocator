#pragma once
#include "memlab/sched/scheduler.hpp"
#include <queue>
#include <vector>

namespace memlab {

/// Shortest Job First — non-preemptive.
/// Selects the process with the smallest remaining CPU burst.
class SJF : public IScheduler {
public:
    void add(Process* p) override { _q.push(p); }

    Process* pick_next() override {
        if (_q.empty()) return nullptr;
        return _q.top_and_pop();
    }

    bool should_preempt(const Process*, uint32_t) const override { return false; }
    const char* name() const override { return "SJF"; }

private:
    struct Cmp {
        bool operator()(const Process* a, const Process* b) const {
            return a->burst_remaining > b->burst_remaining; // min-heap
        }
    };

    // min-priority_queue wrapper
    struct MinPQ : std::priority_queue<Process*, std::vector<Process*>, Cmp> {
        Process* top_and_pop() {
            auto* p = this->top(); this->pop(); return p;
        }
    } _q;
};

} // namespace memlab
