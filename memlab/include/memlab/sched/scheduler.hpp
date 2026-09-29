#pragma once
#include "memlab/sched/process.hpp"
#include <cstdint>

namespace memlab {

// ─── IScheduler — abstract interface ─────────────────────────────────────────
struct IScheduler {
    virtual ~IScheduler() = default;

    /// Add a newly-arrived process to the ready queue.
    virtual void add(Process* p) = 0;

    /// Pick the next process to run. Returns nullptr if queue is empty.
    virtual Process* pick_next() = 0;

    /**
     * should_preempt(running, ran_ticks)
     * Return true if the scheduler wants to preempt `running` after
     * it has already run for `ran_ticks` ticks in the current quantum.
     * Non-preemptive policies always return false.
     */
    virtual bool should_preempt(const Process* running, uint32_t ran_ticks) const = 0;

    /// Called when a process returns from I/O/page-fault wait.
    virtual void on_unblock(Process* p) { add(p); }

    /// Name for CSV output / logging.
    virtual const char* name() const = 0;
};

// ─── Context-switch cost model ────────────────────────────────────────────────
struct CsModel {
    uint32_t cost_ticks{0};       // idle ticks burned on each switch
    uint32_t warmup_ticks{0};     // ticks of reduced efficiency after switch
    uint32_t warmup_denom{1};     // efficiency = 1/warmup_denom during warmup
};

// ─── SimClock ─────────────────────────────────────────────────────────────────
/// Monotonic tick counter shared by the simulation engine.
struct SimClock {
    uint32_t tick{0};
    void     advance(uint32_t n = 1) { tick += n; }
};

} // namespace memlab
