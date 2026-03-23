#include <cstddef>
#include <cstdint>
#include <new>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace Memory {

    // Cache line size (common on modern CPUs)
    constexpr size_t CACHE_LINE_SIZE = 64;

    // Align a size or pointer to a power of 2
    inline size_t AlignUp(size_t size, size_t alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    inline void* AlignPtr(void* ptr, size_t alignment) {
        return reinterpret_cast<void*>(AlignUp(reinterpret_cast<uintptr_t>(ptr), alignment));
    }

    // OS-level memory allocation (paging/virtual memory)
    inline void* AllocateVirtualMemory(size_t size) {
#ifdef _WIN32
        return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
        return mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    }

    inline void FreeVirtualMemory(void* ptr, size_t size) {
        if (!ptr) return;
#ifdef _WIN32
        VirtualFree(ptr, 0, MEM_RELEASE);
#else
        munmap(ptr, size);
#endif
    }

} // namespace Memory
