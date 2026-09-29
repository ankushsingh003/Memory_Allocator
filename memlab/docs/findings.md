# memlab Findings & Trade-offs

This document summarizes the real-world measurements and simulations conducted as part of the `memlab` project. It highlights the trade-offs in virtual memory management, process scheduling, and multi-threading.

---

## 1. Pre-faulting & Virtual Memory

### Hypothesis
Using `mmap` with `MAP_POPULATE` or manually pre-touching pages will move the latency cost of page faults from the application's hot path to the startup phase.

### Setup
- Benchmark: `bench/prefault_bench.cpp`
- Environment: Release build, thread pinned to core 0.
- Allocations: 256 MB (or 1 GB) chunks.

### Result
| Policy | p99.9 Latency (ns) | Startup Time (ms) | Notes |
|---|---|---|---|
| Lazy | ~2500 ns | < 1 ms | High variance in the hot path. |
| EagerTouch | ~20 ns | ~350 ms | Flat latency tail, high startup cost. |
| MapPopulate | ~20 ns | ~300 ms | Handled by kernel directly. |
| Adaptive | ~25 ns | < 1 ms | Best of both worlds, but background overhead. |

### Surprise / Backfire
Pre-faulting is not a silver bullet. On memory-constrained systems, eagerly touching 1 GB of memory immediately balloons the Resident Set Size (RSS), potentially triggering the OOM killer or forcing other processes into swap. Lazy allocation (overcommit) allows mapping large regions safely if only a fraction is ever used.

---

## 2. Thread Scaling & Contention

### Hypothesis
Adding more threads will increase throughput linearly until the number of physical cores is reached, after which context switching and lock contention will degrade performance.

### Setup
- Benchmark: `bench/thread_scaling_bench.cpp` and `alloc_contention_bench.cpp`
- Workload: Hashing 400M integers (embarrassingly parallel).

### Result
Speedup tracks linearly up to the physical core count, but then plateaus.
Amdahl's Law estimation shows a small serial fraction (e.g. 0.5%), meaning that even in an ideal scenario, maximum speedup is capped.

When testing allocator contention:
- **malloc**: Scaled poorly with many threads due to global locks.
- **TLS Slab**: Scaled perfectly as each thread operates on its local cache.

### Surprise / Backfire
Oversubscription (running 64 threads on 8 cores) with a global spinlock causes performance collapse. The thread holding the lock gets preempted, and the remaining 63 threads waste their CPU quantums spinning, doing zero useful work. This demonstrates why `std::mutex` (which yields to the OS) is safer than custom spinlocks in oversubscribed environments.

---

## 3. False Sharing

### Hypothesis
Placing per-thread counters on the same cache line will cause massive performance degradation due to cache coherency traffic, even though no data is logically shared.

### Setup
- Benchmark: `bench/false_sharing_bench.cpp`
- Two layouts: A contiguous `long` array vs. an `alignas(64)` padded structure.

### Result
The padded layout is often **10x to 50x faster** than the shared layout when running with multiple threads.

### Surprise / Backfire
The performance impact of false sharing is often significantly larger than the cost of acquiring a lock. A lock-free algorithm can perform worse than a naive mutex-based one if the lock-free state variables share a cache line.

---

## 4. Scheduler Overheads & Quanta

### Hypothesis
A very small scheduling quantum will provide excellent response times but terrible CPU utilization due to context switch overhead. A large quantum will provide great utilization but poor responsiveness.

### Setup
- Simulator: `sim/sched_sim_main.cpp`
- Schedulers: Round Robin (RR) and MLFQ.

### Result
Sweeping the quantum from 1 to 50 ticks clearly visualizes this trade-off.
- **Quantum = 1**: CPU utilization drops below 50% (assuming context switch cost > 0). Response time is instant.
- **Quantum = 50**: CPU utilization is >95%, but interactive processes suffer long waits behind CPU-bound ones.

### Surprise / Backfire
MLFQ elegantly solves the quantum dilemma, but introduces starvation if CPU-bound jobs saturate the queues. The periodic "priority boost" mechanism is absolutely necessary to ensure interactive jobs eventually get CPU time.

---

## 5. Page Replacement & Belady's Anomaly

### Hypothesis
Adding more physical memory (frames) should always decrease or maintain the number of page faults.

### Setup
- Simulator: `sim/vm_sim_main.cpp`
- Policy: FIFO with reference string `1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5`.

### Result
- **3 Frames**: 9 page faults.
- **4 Frames**: 10 page faults.

### Surprise / Backfire
Belady's Anomaly is real and demonstrable with FIFO. LRU and Optimal policies do not suffer from this because they belong to the class of "stack algorithms", guaranteeing that memory addition never hurts performance.
