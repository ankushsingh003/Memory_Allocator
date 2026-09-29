#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

namespace memlab {

/**
 * pin_thread_to_core(core_id)
 *
 * Pins the CALLING thread to the given logical core.
 * Reduces OS-induced migration noise in microbenchmarks.
 *
 * Supports:
 *   - Linux  : pthread_setaffinity_np
 *   - Windows: SetThreadAffinityMask
 *   - Others : no-op with a warning
 *
 * Throws std::runtime_error on failure.
 */
inline void pin_thread_to_core(unsigned core_id) {
#if defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (rc != 0)
        throw std::runtime_error("pin_thread_to_core: pthread_setaffinity_np failed, core="
                                 + std::to_string(core_id));

#elif defined(_WIN32)
    #include <windows.h>
    DWORD_PTR mask = DWORD_PTR{1} << core_id;
    DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), mask);
    if (prev == 0)
        throw std::runtime_error("pin_thread_to_core: SetThreadAffinityMask failed, core="
                                 + std::to_string(core_id));

#else
    // Unsupported platform — silently skip (don't break the build)
    (void)core_id;
#endif
}

/// Returns the number of logical cores available to the process.
inline unsigned hardware_concurrency() noexcept {
    unsigned n = std::thread::hardware_concurrency();
    return (n == 0) ? 1 : n;
}

/**
 * ScopedAffinity — RAII guard that pins this thread to `core_id` on
 * construction and restores the original affinity mask on destruction.
 *
 * Usage:
 *   {
 *     ScopedAffinity pin(0);    // pinned to core 0
 *     // ... benchmark loop ...
 *   }                           // restored
 */
struct ScopedAffinity {
#if defined(_WIN32)
    DWORD_PTR _prev_mask{0};

    explicit ScopedAffinity(unsigned core_id) {
        DWORD_PTR mask = DWORD_PTR{1} << core_id;
        _prev_mask = SetThreadAffinityMask(GetCurrentThread(), mask);
        if (_prev_mask == 0)
            throw std::runtime_error("ScopedAffinity: SetThreadAffinityMask failed");
    }

    ~ScopedAffinity() {
        if (_prev_mask)
            SetThreadAffinityMask(GetCurrentThread(), _prev_mask);
    }

#elif defined(__linux__)
    cpu_set_t _prev{};

    explicit ScopedAffinity(unsigned core_id) {
        pthread_getaffinity_np(pthread_self(), sizeof(_prev), &_prev);
        pin_thread_to_core(core_id);
    }

    ~ScopedAffinity() {
        pthread_setaffinity_np(pthread_self(), sizeof(_prev), &_prev);
    }

#else
    explicit ScopedAffinity(unsigned) {}
    ~ScopedAffinity() {}
#endif

    // Non-copyable, non-movable
    ScopedAffinity(const ScopedAffinity&) = delete;
    ScopedAffinity& operator=(const ScopedAffinity&) = delete;
};

} // namespace memlab
