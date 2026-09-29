#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>

// Platform-specific includes
#if defined(__linux__) || defined(__APPLE__)
  #include <sys/mman.h>
  #include <sys/resource.h>
  #include <unistd.h>
  #define MEMLAB_POSIX 1
#elif defined(_WIN32)
  #include <windows.h>
  #define MEMLAB_WIN32 1
#endif

namespace memlab {

// ─── PrefaultPolicy ──────────────────────────────────────────────────────────
enum class PrefaultPolicy {
    Lazy,           // mmap only — pages faulted on first touch (hot-path cost)
    EagerTouch,     // touch every page at construction (startup cost)
    MapPopulate,    // MAP_POPULATE (Linux) — kernel faults pages inside mmap()
    Adaptive        // background thread pre-touches pages ahead of HWM
};

// ─── VmemRegion ──────────────────────────────────────────────────────────────
/**
 * RAII wrapper around an anonymous memory-mapped region.
 * Move-only — no copies allowed.
 *
 * Construction options:
 *   bytes       — size of the region (rounded up to page boundary)
 *   policy      — when to pay the page-fault cost
 *   huge        — request huge pages (THP on Linux, best-effort)
 *   lock        — mlock() the region so it cannot be swapped out
 *
 * Adaptive mode:
 *   A background thread walks ahead of _hwm_bytes, pre-touching up to
 *   _watermark pages. Use advance_hwm() to report how far the allocator
 *   has actually consumed the region.
 */
class VmemRegion {
public:
    VmemRegion(size_t bytes,
               PrefaultPolicy policy = PrefaultPolicy::Lazy,
               bool huge  = false,
               bool lock  = false)
        : _size(round_up_to_page(bytes))
        , _policy(policy)
        , _locked(false)
        , _prefaulted(0)
        , _hwm_bytes(0)
        , _stop_bg(false)
    {
        _base = map_region(_size, policy, huge);

        switch (policy) {
            case PrefaultPolicy::Lazy:
            case PrefaultPolicy::MapPopulate:
                // Nothing extra — either already done by kernel or deferred.
                _prefaulted = (policy == PrefaultPolicy::MapPopulate) ? _size : 0;
                break;

            case PrefaultPolicy::EagerTouch:
                eager_touch(_base, _size);
                _prefaulted = _size;
                break;

            case PrefaultPolicy::Adaptive:
                // Start background pre-toucher
                _bg_thread = std::thread(&VmemRegion::adaptive_worker, this);
                break;
        }

        if (lock) {
            do_mlock();
        }
    }

    ~VmemRegion() {
        // Stop adaptive worker first
        if (_bg_thread.joinable()) {
            _stop_bg.store(true, std::memory_order_relaxed);
            _bg_thread.join();
        }
        if (_locked) do_munlock();
        unmap_region(_base, _size);
    }

    // Move-only
    VmemRegion(VmemRegion&& o) noexcept
        : _base(o._base), _size(o._size), _policy(o._policy)
        , _locked(o._locked), _prefaulted(o._prefaulted.load())
        , _hwm_bytes(o._hwm_bytes.load()), _stop_bg(false)
    {
        o._base = nullptr;
        o._size = 0;
        o._locked = false;
        if (o._bg_thread.joinable()) {
            o._stop_bg.store(true, std::memory_order_relaxed);
            o._bg_thread.join();
        }
    }

    VmemRegion& operator=(VmemRegion&&) = delete;
    VmemRegion(const VmemRegion&) = delete;
    VmemRegion& operator=(const VmemRegion&) = delete;

    // ── Accessors ──────────────────────────────────────────────────────────
    void*  base()             const noexcept { return _base; }
    size_t size()             const noexcept { return _size; }
    /// Bytes confirmed pre-faulted (approximate for Adaptive policy).
    size_t prefaulted_bytes() const noexcept {
        return _prefaulted.load(std::memory_order_relaxed);
    }

    /**
     * advance_hwm — tell the adaptive worker how far the allocator has
     * consumed. The worker tries to stay `kWatermarkPages` pages ahead.
     */
    void advance_hwm(size_t consumed_bytes) noexcept {
        _hwm_bytes.store(consumed_bytes, std::memory_order_relaxed);
    }

private:
    void*          _base{nullptr};
    size_t         _size{0};
    PrefaultPolicy _policy;
    bool           _locked;
    std::atomic<size_t> _prefaulted;
    std::atomic<size_t> _hwm_bytes;
    std::atomic<bool>   _stop_bg;
    std::thread         _bg_thread;

    static constexpr size_t kWatermarkPages = 64; // pages ahead of HWM to pre-touch

    // ── Platform: map ─────────────────────────────────────────────────────
    static void* map_region(size_t sz, PrefaultPolicy policy, bool huge) {
#if defined(MEMLAB_POSIX)
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;

#if defined(__linux__)
        if (policy == PrefaultPolicy::MapPopulate) flags |= MAP_POPULATE;
        if (huge)                                   flags |= MAP_HUGETLB;
#endif
        void* p = ::mmap(nullptr, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (p == MAP_FAILED)
            throw std::runtime_error("VmemRegion: mmap failed: " + std::to_string(sz) + " bytes");

#if defined(__linux__)
        if (huge && !(flags & MAP_HUGETLB)) {
            // Fallback: request Transparent Huge Pages
            ::madvise(p, sz, MADV_HUGEPAGE);
        }
#endif
        return p;

#elif defined(MEMLAB_WIN32)
        (void)policy; (void)huge;
        void* p = VirtualAlloc(nullptr, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) throw std::runtime_error("VmemRegion: VirtualAlloc failed");
        return p;
#else
        throw std::runtime_error("VmemRegion: unsupported platform");
#endif
    }

    static void unmap_region(void* p, size_t sz) {
        if (!p || !sz) return;
#if defined(MEMLAB_POSIX)
        ::munmap(p, sz);
#elif defined(MEMLAB_WIN32)
        VirtualFree(p, 0, MEM_RELEASE);
#endif
    }

    // ── EagerTouch ────────────────────────────────────────────────────────
    static void eager_touch(void* base, size_t sz) {
        size_t page_sz = page_size();
        volatile char* p = static_cast<volatile char*>(base);
        for (size_t off = 0; off < sz; off += page_sz)
            p[off] = 0;   // write to commit the physical page
    }

    // ── Adaptive worker ───────────────────────────────────────────────────
    void adaptive_worker() {
        size_t page_sz  = page_size();
        size_t prefaulted_off = 0;   // how far we've pre-touched

        while (!_stop_bg.load(std::memory_order_relaxed)) {
            size_t hwm = _hwm_bytes.load(std::memory_order_relaxed);
            size_t target = hwm + kWatermarkPages * page_sz;
            if (target > _size) target = _size;

            if (prefaulted_off < target) {
                // Pre-touch pages from prefaulted_off to target
#if defined(__linux__) && defined(MADV_POPULATE_WRITE)
                ::madvise(static_cast<char*>(_base) + prefaulted_off,
                          target - prefaulted_off,
                          MADV_POPULATE_WRITE);
#else
                // Fallback: manual touch
                volatile char* p = static_cast<volatile char*>(_base) + prefaulted_off;
                for (size_t off = 0; off < (target - prefaulted_off); off += page_sz)
                    p[off] = 0;
#endif
                prefaulted_off = target;
                _prefaulted.store(prefaulted_off, std::memory_order_relaxed);
            }

            // Sleep briefly and re-check
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    // ── mlock / munlock ───────────────────────────────────────────────────
    void do_mlock() {
#if defined(MEMLAB_POSIX)
        if (::mlock(_base, _size) == 0) {
            _locked = true;
        }
        // Silently ignore failure (RLIMIT_MEMLOCK may be low)
#elif defined(MEMLAB_WIN32)
        if (VirtualLock(_base, _size)) _locked = true;
#endif
    }

    void do_munlock() {
#if defined(MEMLAB_POSIX)
        ::munlock(_base, _size);
#elif defined(MEMLAB_WIN32)
        VirtualUnlock(_base, _size);
#endif
        _locked = false;
    }

    // ── Utilities ─────────────────────────────────────────────────────────
    static size_t page_size() noexcept {
#if defined(MEMLAB_POSIX)
        static size_t ps = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
        return ps;
#elif defined(MEMLAB_WIN32)
        static size_t ps = []{ SYSTEM_INFO si; GetSystemInfo(&si); return si.dwPageSize; }();
        return ps;
#else
        return 4096;
#endif
    }

    static size_t round_up_to_page(size_t bytes) noexcept {
        size_t ps = page_size();
        return (bytes + ps - 1) & ~(ps - 1);
    }
};

// ─── Convenience: page_fault_stats ───────────────────────────────────────────
/**
 * Snapshot of minor/major fault counts for the current process.
 * Take two snapshots (before/after) and diff them.
 */
struct FaultStats {
    long minor_faults{0};
    long major_faults{0};

    static FaultStats capture() {
        FaultStats s;
#if defined(MEMLAB_POSIX)
        struct rusage ru{};
        ::getrusage(RUSAGE_SELF, &ru);
        s.minor_faults = ru.ru_minflt;
        s.major_faults = ru.ru_majflt;
#endif
        return s;
    }

    FaultStats operator-(const FaultStats& o) const noexcept {
        return { minor_faults - o.minor_faults, major_faults - o.major_faults };
    }
};

} // namespace memlab
