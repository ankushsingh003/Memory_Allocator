#pragma once
#include "memlab/sched/scheduler.hpp"
#include <queue>
#include <vector>

namespace memlab {

/// Shortest Remaining Time First — preemptive SJF.
/// A new arrival with shorter burst preempts the running process.
class SRTF : public IScheduler {
public:
    void add(Process* p) override { _q.push(p); }

    Process* pick_next() override {
        if (_q.empty()) return nullptr;
        auto* p = _q.top(); _q.pop(); return p;
    }

    bool should_preempt(const Process* running, uint32_t) const override {
        if (_q.empty()) return false;
        return _q.top()->burst_remaining < running->burst_remaining;
    }

    void on_unblock(Process* p) override { add(p); }
    const char* name() const override { return "SRTF"; }

private:
    struct Cmp {
        bool operator()(const Process* a, const Process* b) const {
            return a->burst_remaining > b->burst_remaining;
        }
    };
    std::priority_queue<Process*, std::vector<Process*>, Cmp> _q;
};

} // namespace memlab
