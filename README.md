# 🚀 High-Performance Custom Memory Allocator

A production-grade, multi-threaded memory management suite for C++. Designed for low-latency systems (HFT, Game Engines, Kernels) where standard `malloc` is too slow or introduces non-deterministic jitter.

![C++](https://img.shields.io/badge/C++-20-blue.svg)
![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20|%20TSan-green.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

## 🌟 Key Features

### 1. 🏎️ Ultra-Fast Pool Allocator
*   **Best for**: High-frequency, fixed-size objects (e.g., Network Packets, ECS Components).
*   **Performance**: **O(1)** allocation and deallocation via a lock-free freelist.
*   **Results**: ~15x faster than `std::malloc`.

### 2. 🧵 Thread-Safe Slab Cache (TLS)
*   **Architecture**: Kernel-inspired design using **Thread-Local Storage (TLS)** magazines.
*   **Fast Path**: Core-local allocation without mutex contention.
*   **128-bit Lock-Free**: Uses **Atomic Tagged Pointers** to eliminate the **ABA Problem** in high-concurrency environments.

### 🧩 3. Fragmentation-Resistant Buddy System
*   **Algorithm**: Power-of-two block splitting and recursive coalescing.
*   **STL Ready**: Includes a drop-in `std::allocator` wrapper for usage with `std::vector`, `std::list`, and other standard containers.

---

## 📊 Benchmarks (Representative Results)

| Allocator | Op Time (Latency) | Throughput (MT/s) | Speedup vs Malloc |
| :--- | :--- | :--- | :--- |
| **LinearArena** | **~1.2 ns** | ~800+ | **120x** |
| **PoolAllocator** | **~12 ns** | ~80+ | **15x** |
| **SlabCache (TLS)**| **~18 ns** | ~50+ | **10x** |
| **System Malloc** | **~180 ns** | ~5 | **Base** |

*Benchmarks conducted on an 8-core CPU (1,000,000 iterations per test).*

---

## 🛠️ Architecture Overview

```mermaid
graph TD
    User["Application (std::vector)"] --> Wrapper["Memory::Allocator&lt;T&gt;"]
    Wrapper --> Engine["Buddy System / Slab Cache"]
    Engine --> HotPath["TLS Magazine (Fast Path)"]
    Engine --> GlobalSlabs["Global Slab List (Slow Path)"]
    GlobalSlabs --> Raw["VirtualMemory (mmap / VirtualAlloc)"]
```

---

## 🚦 Verification & Correctness

This project is built with a "Safety-First" mindset:
*   **AddressSanitizer (ASan)**: Verified clean of leaks, overflows, and double-frees.
*   **ThreadSanitizer (TSan)**: Verified race-free lock-free logic (128-bit CAS).
*   **GoogleTest**: Comprehensive suite covering OOM, Alignment, and ABA scenarios.

## 🚀 Getting Started

### Build Requirements
*   CMake 3.15+
*   C++20 compliant compiler (GCC 10+, Clang 11+, MSVC 2019+)

### Build Instructions
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Run Benchmarks
```bash
./alloc_bench
```

### Run Demo (Packet Simulation)
```bash
./packet_demo
```

---

## 🖥️ OS Process & Memory Simulator (new)

On top of the allocator core, this project includes a small but real
**process scheduler** that ties process lifecycle directly to the
`BuddyAllocator`, and a browser-based **visualizer** to watch it run.

This isn't a scripted animation — the C++ program actually schedules
processes, blocks them on I/O, and calls into the real allocator to grant
and free memory. The visualizer just replays the resulting event trace.

**What it demonstrates:**
- Process lifecycle: `NEW → READY → RUNNING → WAITING (I/O) → TERMINATED`
- Round-robin CPU scheduling with quantum-based preemption
- **Memory-constrained admission**: a process only becomes `READY` once the
  allocator can actually grant its memory request — if the arena is full,
  it stays pending, which is a simplified but real model of memory pressure
- CPU bursts, I/O bursts, and I/O acknowledgement
- Real allocator activity: contiguous byte-addressed allocation, buddy
  splitting/coalescing, and freeing on process termination

### Build & run the simulation
```bash
g++ -std=c++20 -Iinclude examples/OSSimDemo.cpp -o os_sim_demo
./os_sim_demo
```
This writes `events.json` — a full trace of every process/memory event,
tick by tick. (It's also wired into `CMakeLists.txt` as the `os_sim_demo`
target if you're building the whole project via CMake.)

### Watch it
Open `frontend/index.html` directly in a browser (no server needed) and
load the `events.json` file you just generated using the file picker.
Then use **Step** to go event-by-event, or **Play** to watch it run
automatically:

- **Memory arena strip** — the whole managed arena, drawn to scale, colored
  by which process owns each block, with real byte offsets
- **Process state lanes** — Ready / Running / Waiting / Terminated, with
  processes visibly moving between them
- **Kernel log** — every scheduling and memory event, in order

A sample `frontend/events.json` is included so you can open the visualizer
immediately without building anything first.

---

## 📄 License
Distributed under the MIT License. See `LICENSE` for more information.
