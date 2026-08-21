#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace OS {

    // Classic 5-state process model.
    enum class ProcessState {
        NEW,        // created, not yet admitted (may be waiting on memory)
        READY,      // admitted, in the ready queue, waiting for CPU
        RUNNING,    // currently on the CPU
        WAITING,    // blocked on I/O
        TERMINATED  // finished, memory freed
    };

    inline const char* ToString(ProcessState s) {
        switch (s) {
            case ProcessState::NEW:         return "NEW";
            case ProcessState::READY:       return "READY";
            case ProcessState::RUNNING:     return "RUNNING";
            case ProcessState::WAITING:     return "WAITING";
            case ProcessState::TERMINATED:  return "TERMINATED";
        }
        return "UNKNOWN";
    }

    // A single CPU burst, optionally followed by an I/O burst.
    // A process is a sequence of these, e.g. CPU 4 -> IO 3 -> CPU 2 -> (done).
    struct Burst {
        int cpuTicks;
        int ioTicks; // 0 means "no I/O after this CPU burst" (process may terminate here)
    };

    // Process Control Block (simplified).
    struct Process {
        int id;
        std::string name;

        int arrivalTick;          // when it becomes eligible for admission
        size_t memoryRequest;     // bytes requested from the allocator

        std::vector<Burst> bursts;
        size_t currentBurst = 0;
        int remainingCpuTicks = 0; // remaining ticks in the current CPU burst
        int remainingIoTicks = 0;  // remaining ticks in the current I/O wait

        ProcessState state = ProcessState::NEW;

        // Filled in once the allocator grants memory.
        void* memPtr = nullptr;
        size_t memOffset = 0;   // offset into the arena, for visualization
        size_t memBlockSize = 0; // actual granted block size (rounded up by allocator)

        [[nodiscard]] bool HasMoreBursts() const { return currentBurst < bursts.size(); }
    };

} // namespace OS
