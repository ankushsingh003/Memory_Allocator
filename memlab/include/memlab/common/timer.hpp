#pragma once
#include <cstdint>
#include <chrono>
#include <thread>

namespace memlab {

// ─── rdtsc ────────────────────────────────────────────────────────────────────
// Serializing read of the Time Stamp Counter.
// lfence ensures all prior instructions retire before the counter is read,
// giving a stable "start" timestamp. For "stop", use rdtscp which is
// self-serializing on the instruction side.

#if defined(_MSC_VER)
  #include <intrin.h>
  inline uint64_t rdtsc_start() { _mm_lfence(); return __rdtsc(); }
  inline uint64_t rdtsc_stop()  {
      unsigned int aux;
      uint64_t v = __rdtscp(&aux);
      _mm_lfence();
      return v;
  }
#elif defined(__x86_64__) || defined(__i386__)
  inline uint64_t rdtsc_start() noexcept {
      uint32_t lo, hi;
      __asm__ volatile("lfence\n\trdtsc" : "=a"(lo), "=d"(hi));
      return (static_cast<uint64_t>(hi) << 32) | lo;
  }
  inline uint64_t rdtsc_stop() noexcept {
      uint32_t lo, hi;
      __asm__ volatile("rdtscp\n\tlfence" : "=a"(lo), "=d"(hi) :: "ecx");
      return (static_cast<uint64_t>(hi) << 32) | lo;
  }
#else
  // Fallback for non-x86 (ARM64, etc.) — use steady_clock in nanoseconds
  inline uint64_t rdtsc_start() noexcept {
      return static_cast<uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count());
  }
  inline uint64_t rdtsc_stop() noexcept { return rdtsc_start(); }
#endif

// ─── Calibration ──────────────────────────────────────────────────────────────

/**
 * Calibrate TSC: returns the number of TSC ticks per nanosecond.
 * Call once at startup. Takes ~50 ms.
 */
inline double tsc_ticks_per_ns() {
    using clock = std::chrono::steady_clock;
    constexpr int kSamples = 5;
    constexpr auto kDelay  = std::chrono::milliseconds(10);

    double best = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        auto t0 = clock::now();
        uint64_t c0 = rdtsc_start();
        std::this_thread::sleep_for(kDelay);
        uint64_t c1 = rdtsc_stop();
        auto t1 = clock::now();

        double ns  = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        double ticks_per_ns = static_cast<double>(c1 - c0) / ns;
        if (ticks_per_ns > best) best = ticks_per_ns;
    }
    return best;
}

// ─── SteadyTimer ──────────────────────────────────────────────────────────────

/**
 * Lightweight wall-clock timer using steady_clock.
 * ~20-30 ns overhead per call — fine for coarse timing (startup costs, etc.).
 * For sub-100 ns measurements, prefer rdtsc_start/stop + tsc_ticks_per_ns.
 */
struct SteadyTimer {
    using clock = std::chrono::steady_clock;
    using tp    = clock::time_point;

    tp _start{};

    void start() noexcept { _start = clock::now(); }

    /// Elapsed nanoseconds since start().
    int64_t elapsed_ns() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock::now() - _start).count();
    }

    int64_t elapsed_us() const noexcept { return elapsed_ns() / 1'000; }
    int64_t elapsed_ms() const noexcept { return elapsed_ns() / 1'000'000; }
};

/// RAII guard: records elapsed ns into *out on destruction.
struct TimerGuard {
    SteadyTimer t;
    int64_t*    out;
    explicit TimerGuard(int64_t* out) : out(out) { t.start(); }
    ~TimerGuard() { *out = t.elapsed_ns(); }
};

} // namespace memlab
