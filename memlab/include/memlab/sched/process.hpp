#pragma once
#include <cstdint>
#include <climits>
#include <string>
#include <vector>

namespace memlab {

// ─── Process state machine ────────────────────────────────────────────────────
enum class State { New, Ready, Running, Waiting, Terminated };

inline const char* state_name(State s) {
    switch (s) {
        case State::New:        return "NEW";
        case State::Ready:      return "READY";
        case State::Running:    return "RUNNING";
        case State::Waiting:    return "WAITING";
        case State::Terminated: return "TERMINATED";
    }
    return "?";
}

// ─── Burst ────────────────────────────────────────────────────────────────────
struct Burst {
    enum class Type { CPU, IO, PageFault } type;
    uint32_t ticks;
};

// ─── Process ──────────────────────────────────────────────────────────────────
struct Process {
    int      pid{-1};
    std::string name;
    uint32_t arrival{0};
    uint32_t priority{0};       // lower = higher priority
    uint32_t mem_bytes{0};

    std::vector<Burst> bursts;
    size_t   cur_burst{0};      // index into bursts[]
    uint32_t burst_remaining{0};// ticks left in current burst

    State    state{State::New};

    // ── Metrics ──────────────────────────────────────────────────────────
    uint32_t first_run{UINT32_MAX};
    uint32_t finish{0};
    uint32_t wait_ticks{0};     // accumulated time in Ready state
    uint32_t io_ticks{0};       // accumulated time in Waiting state
    uint32_t ctx_switches{0};

    // ── Derived metrics (computed after finish) ───────────────────────────
    uint32_t turnaround() const { return finish - arrival; }
    uint32_t response()   const { return (first_run == UINT32_MAX) ? 0 : first_run - arrival; }
    uint32_t total_cpu_burst() const {
        uint32_t t = 0;
        for (auto& b : bursts)
            if (b.type == Burst::Type::CPU) t += b.ticks;
        return t;
    }

    // Convenience: advance to next burst, return false if done
    bool advance_burst() {
        ++cur_burst;
        if (cur_burst >= bursts.size()) return false;
        burst_remaining = bursts[cur_burst].ticks;
        return true;
    }

    bool has_more_bursts() const { return cur_burst < bursts.size(); }
    Burst* current_burst() {
        if (cur_burst >= bursts.size()) return nullptr;
        return &bursts[cur_burst];
    }
};

} // namespace memlab
