#pragma once
#include <cstddef>
#include <cstdint>

namespace memlab {

/// The cache-line size on virtually all x86/ARM64 hardware.
inline constexpr std::size_t kCacheLine = 64;

/**
 * padded<T> — wraps T so it occupies exactly one cache line.
 * Use for per-thread counters / stats structs to eliminate false sharing.
 *
 * Example:
 *   padded<long> counters[N];   // each counter on its own cache line
 *   counters[i].value++;
 */
template <typename T>
struct alignas(kCacheLine) padded {
    T value{};

    padded() = default;
    explicit padded(T v) : value(v) {}

    // Implicit conversions for ergonomic use
    operator T&()             { return value; }
    operator const T&() const { return value; }

    padded& operator=(const T& v) { value = v; return *this; }
    padded& operator+=(T v)       { value += v; return *this; }
    padded& operator-=(T v)       { value -= v; return *this; }
    T operator++()                { return ++value; }
    T operator++(int)             { return value++; }

private:
    // Padding to fill out the rest of the cache line
    char _pad[kCacheLine - sizeof(T)];

    static_assert(sizeof(T) <= kCacheLine,
        "padded<T>: T is larger than one cache line — no padding possible.");
};

/// Utility: does ptr have the required alignment?
inline constexpr bool is_aligned(const void* ptr, std::size_t align) noexcept {
    return (reinterpret_cast<std::uintptr_t>(ptr) & (align - 1)) == 0;
}

/// Utility: round up v to the next multiple of align (align must be power-of-two).
inline constexpr std::size_t align_up(std::size_t v, std::size_t align) noexcept {
    return (v + align - 1) & ~(align - 1);
}

} // namespace memlab
