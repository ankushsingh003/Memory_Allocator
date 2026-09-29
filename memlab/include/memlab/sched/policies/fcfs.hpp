#pragma once
#include "memlab/sched/scheduler.hpp"
#include <queue>

namespace memlab {

/// First-Come First-Served — non-preemptive, FIFO queue.
class FCFS : public IScheduler {
public:
    void add(Process* p) override { _q.push(p); }

    Process* pick_next() override {
        if (_q.empty()) return nullptr;
        auto* p = _q.front(); _q.pop();
        return p;
    }

    bool should_preempt(const Process*, uint32_t) const override { return false; }
    const char* name() const override { return "FCFS"; }

private:
    std::queue<Process*> _q;
};

} // namespace memlab
