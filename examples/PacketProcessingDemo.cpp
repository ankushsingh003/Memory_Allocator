#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include "PoolAllocator.hpp"

// Simulates a network packet (e.g., Ethernet frame metadata)
struct Packet {
    uint64_t timestamp;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t length;
    uint8_t  payload[32]; // Small fixed payload for simulation
};

void RunSimulation(size_t iterations) {
    std::cout << "--- Packet Processing Simulation (1,000,000 packets) ---\n" << std::endl;

    // 1. Baseline: System Heap (new/delete)
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < iterations; ++i) {
            Packet* p = new Packet();
            p->timestamp = i;
            // Simulate some "processing"
            volatile uint64_t dummy = p->timestamp; 
            delete p;
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms = end - start;
        std::cout << "System Heap (new/delete): " << std::fixed << std::setprecision(2) << ms.count() << " ms" << std::endl;
    }

    // 2. Optimized: Pool Allocator
    {
        Memory::PoolAllocator<Packet> pool(iterations); 
        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < iterations; ++i) {
            Packet* p = pool.Allocate();
            p->timestamp = i;
            // Simulate same "processing"
            volatile uint64_t dummy = p->timestamp;
            pool.Free(p);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms = end - start;
        std::cout << "Custom Pool Allocator:    " << std::fixed << std::setprecision(2) << ms.count() << " ms" << std::endl;
        
        double speedup = (iterations > 0) ? (iterations) : 1.0; // Avoid division
        // Note: Real speedup is roughly (HeapTime / PoolTime)
    }

    std::cout << "\nResult: The Pool Allocator eliminates OS system calls and heap fragmentation overhead." << std::endl;
}

int main() {
    RunSimulation(1000000);
    return 0;
}
