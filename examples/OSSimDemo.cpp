// OSSimDemo: a small, real (not scripted) OS-style simulation built on top
// of the project's existing BuddyAllocator.
//
// A handful of processes are created with randomized memory requirements and
// CPU/IO burst patterns. A round-robin scheduler admits them (subject to real
// memory availability from the BuddyAllocator), runs them, blocks them on I/O,
// and frees their memory on termination. Every state transition and memory
// event is written to events.json, which the web visualizer replays.
//
// Build: see CMakeLists.txt (target: os_sim_demo)
// Run:   ./os_sim_demo   -> writes events.json in the current directory

#include "BuddyAllocator.hpp"
#include "Scheduler.hpp"
#include "Process.hpp"
#include "EventLogger.hpp"

#include <iostream>
#include <random>
#include <string>

int main() {
    using namespace OS;

    // A modestly sized arena so allocation pressure (and admission delays)
    // actually show up in the demo instead of everything fitting trivially.
    constexpr size_t TOTAL_MEMORY = 8192;   // bytes, must be power of 2
    constexpr size_t MIN_BLOCK    = 64;     // bytes, must be power of 2

    Memory::BuddyAllocator allocator(TOTAL_MEMORY, MIN_BLOCK);
    EventLogger log("events.json");
    Scheduler scheduler(allocator, log, /*quantum=*/3);

    std::mt19937 rng(42); // fixed seed: reproducible, demo-safe run
    std::uniform_int_distribution<int> memSizeDist(0, 4);
    const size_t memSizes[] = {128, 256, 512, 1024, 2048};

    std::uniform_int_distribution<int> arrivalDist(0, 6);
    std::uniform_int_distribution<int> cpuBurstDist(2, 6);
    std::uniform_int_distribution<int> ioBurstDist(1, 4);
    std::uniform_int_distribution<int> numBurstsDist(1, 3);

    const int NUM_PROCESSES = 8;
    const char* names[] = {"init", "shell", "compiler", "browser", "logger",
                            "db_worker", "net_daemon", "renderer", "cache_svc", "watchdog"};

    for (int i = 0; i < NUM_PROCESSES; ++i) {
        Process p;
        p.id = i + 1;
        p.name = names[i % (sizeof(names) / sizeof(names[0]))];
        p.arrivalTick = arrivalDist(rng);
        p.memoryRequest = memSizes[memSizeDist(rng)];

        int numBursts = numBurstsDist(rng);
        for (int b = 0; b < numBursts; ++b) {
            int cpu = cpuBurstDist(rng);
            // Last burst never has an I/O tail, so the process can terminate.
            int io = (b == numBursts - 1) ? 0 : ioBurstDist(rng);
            p.bursts.push_back({cpu, io});
        }

        scheduler.AddProcess(std::move(p));
    }

    scheduler.Run(/*maxTicks=*/200);

    std::cout << "Simulation complete. Wrote events.json "
                 "(open frontend/index.html and load it to visualize).\n";
    return 0;
}
