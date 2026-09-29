# memlab — Implementation Blueprint

> Full design spec for the prefault, VM simulator, scheduler, and threading experiments.

---

## Part A — Prefault Module (Real Virtual Memory)

### Design

```cpp
enum class PrefaultPolicy { Lazy, EagerTouch, MapPopulate, Adaptive };

class VmemRegion {              // RAII, move-only
public:
  VmemRegion(size_t bytes, PrefaultPolicy p, bool huge = false, bool lock = false);
  ~VmemRegion();                // munmap
  void* base() const;
  size_t size() const;
  size_t prefaulted_bytes() const;
};
```

### Implementation Notes

| Policy | Mechanism |
|---|---|
| Lazy | mmap(nullptr, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) — nothing else |
| MapPopulate | Add MAP_POPULATE to flags. Kernel faults everything in inside the call |
| EagerTouch | Loop with stride sysconf(_SC_PAGESIZE), write one byte per page via volatile* |
| Adaptive | Background thread keeps watermark of N pre-touched pages ahead of allocator HWM via madvise(addr, len, MADV_POPULATE_WRITE) (Linux 5.14+) |
| Huge pages | MAP_HUGETLB (needs reserved huge pages) or madvise(MADV_HUGEPAGE) (THP, no reservation) |
| mlock | mlock(base, size) — handle failure, RLIMIT_MEMLOCK is often small |

### Pitfalls

- A read does not commit memory. Reading an untouched anonymous page maps the shared zero page. Always write.
- Store latency samples in a pre-allocated array — the benchmark's own allocations add noise.
- steady_clock costs ~20-30 ns per call. Use rdtsc (with lfence) for nanosecond-level timing and calibrate against wall time.

---

### Benchmark: bench/prefault_bench.cpp

Allocate 1 GB, touch page by page, record every latency.

| Measure | How |
|---|---|
| p50, p99, p99.9 per touch | Sorted latency array |
| Startup time | Time around region construction |
| Page faults | getrusage: ru_minflt, ru_majflt before and after |
| TLB misses | perf stat -e dTLB-load-misses,page-faults ./prefault_bench |

Expected picture: Lazy -> low startup, high-latency tail. Eager -> slow startup, flat fast tail.

### Failure Demos (the backfire)

- Overcommit: allocate more than free RAM under a memory cap and show OOM kill or swapping.
- Major faults under swap pressure: show ru_majflt spiking and p99 going from us to ms.
- NUMA first-touch (multi-socket only): pre-fault from wrong thread, compare latency.
- Startup cost vs waste: pre-fault 4 GB, touch only 10%, report wasted RSS.
- THP compaction stalls: compare madvise on vs off, worst-case latency.

---

## Part B — Virtual Memory Simulator (Software)

### Core Interfaces

```cpp
struct PTE { uint32_t frame; bool present, dirty, referenced; };

struct IReplacement {
  virtual void on_load(uint32_t frame) = 0;
  virtual void on_access(uint32_t frame) = 0;
  virtual uint32_t victim() = 0;
  virtual ~IReplacement() = default;
};

struct AccessResult { bool tlb_hit, page_fault, evicted; uint64_t cost_ticks; };
AccessResult access(pid_t pid, uint64_t vaddr, bool is_write);
```

### Access Flow

1. Split vaddr into page_number and offset.
2. Check TLB. On hit: cost = t_tlb + t_mem.
3. On TLB miss: walk page table. If present, refill TLB, cost = t_tlb + t_pt + t_mem.
4. If not present: page fault. Take free frame or ask policy for victim. If victim is dirty, add disk write cost. Load page, update PTE, flush victim TLB entry.

Configurable costs: TLB=1ns, Memory=100ns, Disk=10,000,000ns.

### Replacement Policies

| Policy | Data Structure | Complexity |
|---|---|---|
| FIFO | Queue | O(1) |
| LRU | Doubly linked list + hash map | O(1) |
| Clock | Circular buffer + reference bits | O(1) amortized |
| Optimal (Belady) | Offline, needs full trace | Lower bound baseline |

### Experiments

- Belady's anomaly: ref string 1 2 3 4 1 2 5 1 2 3 4 5 with FIFO: 9 faults at 3 frames, 10 faults at 4 frames -> unit test.
- Fault rate vs frames for each policy and workload.
- Thrashing: P processes sharing F frames. As P grows, plot fault rate and CPU utilization collapse.
- EAT: compute from measured hit/fault rates, compare with formula.
- Connect to scheduler: page fault moves process to WAITING for fault_service_ticks.

---

## Part C — Scheduler Extension

### Structures

```cpp
struct Burst { enum Type { CPU, IO } type; uint32_t ticks; };

struct Process {
  pid_t pid; uint32_t arrival, priority, mem_bytes;
  std::vector<Burst> bursts; size_t cur;
  State state;
  uint32_t first_run = UINT32_MAX, finish = 0, wait = 0;
};

struct IScheduler {
  virtual void add(Process*) = 0;
  virtual Process* pick_next() = 0;
  virtual bool should_preempt(Process* running, uint32_t ran_ticks) = 0;
  virtual ~IScheduler() = default;
};
```

### Policies

| Policy | Notes |
|---|---|
| FCFS | Non-preemptive, arrival order |
| SJF | Non-preemptive, shortest burst first |
| SRTF | Preemptive SJF |
| Round Robin | Fixed quantum |
| Priority | With aging to prevent starvation |
| MLFQ | 3 queues, demote on quantum expiry, periodic boost |

### Context-Switch Cost Model

When CPU switches from A to B (A != B):
- Burn cs_cost idle ticks.
- Run next warmup_ticks of B at reduced efficiency (1 tick progress per 2 ticks) to model cache cold-start.

### Metrics

| Metric | Formula |
|---|---|
| CPU utilization | useful_ticks / total_ticks |
| Turnaround | finish - arrival |
| Waiting | turnaround - total_cpu_burst |
| Response | first_run - arrival |
| Context switches | Count + overhead ticks |

### Sweeps (output CSV)

- Quantum 1-50 vs utilization and avg response time.
- cs_cost 0, 1, 5, 10 across all schedulers.
- All policies on same seeded workload: CPU-heavy, I/O-heavy, mixed.

---

## Part D — Real Threading Experiments

### D.1 Thread Scaling (bench/thread_scaling_bench.cpp)

- Fixed total work W (e.g., hash 400M integers), split across T threads.
- Sweep T = 1, 2, 4, 8, 16, 32, 64.
- Use std::barrier so timing starts after all threads are created.
- Report speedup = T1/Tn and efficiency = speedup/T.
- Fit Amdahl's law to estimate the serial fraction.

### D.2 Context-Switch Cost (bench/ctx_switch_bench.cpp)

- Ping-pong between two threads via pipe or condition variable.
- 1M round trips -> cost per switch = total / (2 * 1M).
- Compare: same core vs different cores.

### D.3 Oversubscription: Spin vs Block

- Compare spinlock vs std::mutex with more threads than cores.
- Spinlocks collapse because lock holder gets preempted while others burn quantum spinning.

### D.4 False Sharing (bench/false_sharing_bench.cpp)

```cpp
long counters[T];                          // Bad: same cache line
struct alignas(64) { long v; } counters[T]; // Good: padded
```

### D.5 Allocator Contention (bench/alloc_contention_bench.cpp)

T threads, M alloc/free cycles of 64 bytes each:
- malloc (baseline) -> global-mutex wrapper -> PoolAllocator -> TLS SlabCache (best)

### D.6 Task Granularity

- Sweep task size 1us to 10ms.
- Compare: thread-per-task vs thread pool vs single-threaded loop.
- Tiny tasks make threading a net loss.

---

## Measurement Rules

1. Build with -O2 (Release), not Debug.
2. Warm-up run before measuring.
3. At least 10 runs — report median and p99, not mean.
4. Pin threads (pthread_setaffinity_np), keep machine idle.
5. Prevent dead-code elimination: asm volatile("" : "+r"(x)).
6. Record CPU model, core count, RAM, kernel, compiler in every CSV header.
7. Never print or allocate inside timed loops.
8. Keep per-thread stats in alignas(64) padded structs.

---

## Testing Checklist

| Component | Test |
|---|---|
| Replacement policies | Unit-test against hand-computed fault counts (Belady's string; LRU loop worst-case) |
| Scheduler | Fixed seed -> identical schedule; total CPU ticks conserved |
| VmemRegion | RAII under ASan; prefaulted_bytes() matches getrusage fault deltas |
| Adaptive prefault thread | TSan on all background thread logic |

---

## Build Order

1. common/ (timer, stats, CSV) + scripts/plot.py -> every step gets charts
2. VmemRegion: Lazy vs EagerTouch -> prefault benchmark
3. Thread scaling, ctx-switch, false-sharing benchmarks
4. Scheduler policies -> cs_cost and quantum sweeps
5. Page table, TLB, FIFO/LRU/Clock/Optimal -> Belady unit test
6. Thrashing experiment -> integrate VM into scheduler
7. Adaptive prefault, huge pages, failure demos
8. Write docs/findings.md

---

## findings.md Structure (per experiment)

Each section — 4 parts:

1. **Hypothesis** — what you expected and why
2. **Setup** — hardware, compiler, flags, workload params
3. **Result** — table or chart with real numbers
4. **Surprise / Backfire** — where it didn't work or the idea breaks down

> Example: "Pre-faulting cut p99.9 latency from 2400ns to 18ns but raised startup time by 340ms, and on a memory-limited machine it triggered the OOM killer at 89% allocation."

---

*This document is the source of truth. Start coding only after reviewing the relevant section.*
