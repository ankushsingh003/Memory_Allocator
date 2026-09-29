#pragma once
#include "memlab/sched/scheduler.hpp"
#include <deque>

namespace memlab {

/// Round Robin — preemptive, fixed time quantum.
class RoundRobin : public IScheduler {
public:
    explicit RoundRobin(uint32_t quantum) : _quantum(quantum) {}

    void add(Process* p) override { _q.push_back(p); }

    Process* pick_next() override {
        if (_q.empty()) return nullptr;
        auto* p = _q.front(); _q.pop_front();
        return p;
    }

    bool should_preempt(const Process*, uint32_t ran_ticks) const override {
        return ran_ticks >= _quantum;
    }

    const char* name() const override { return "RR"; }
    uint32_t    quantum() const       { return _quantum; }

private:
    std::deque<Process*> _q;
    uint32_t             _quantum;
};

} // namespace memlab
