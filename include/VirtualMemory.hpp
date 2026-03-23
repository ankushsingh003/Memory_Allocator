#pragma once
#include "MemoryUtils.hpp"
#include <stdexcept>
#include <utility>

namespace Memory {

    /**
     * @brief A Resource Acquisition Is Initialization (RAII) wrapper for OS-level virtual memory.
     * 
     * This class automatically handles the allocation (mmap/VirtualAlloc) and 
     * deallocation (munmap/VirtualFree) of large memory blocks.
     */
    class VirtualMemory {
    public:
        VirtualMemory() = default;

        /**
         * @brief Allocates a block of virtual memory from the OS.
         * @param size The size of the block in bytes.
         */
        explicit VirtualMemory(size_t size) : size_(AlignUp(size, 4096)) {
            ptr_ = AllocateVirtualMemory(size_);
            if (!ptr_) {
                throw std::runtime_error("Failed to allocate virtual memory from the OS.");
            }
        }

        // Destructor handles automatic deallocation (RAII)
        ~VirtualMemory() {
            Free();
        }

        // Disable copying to prevent double-free errors
        VirtualMemory(const VirtualMemory&) = delete;
        VirtualMemory& operator=(const VirtualMemory&) = delete;

        // Enable moving
        VirtualMemory(VirtualMemory&& other) noexcept 
            : ptr_(std::exchange(other.ptr_, nullptr)), 
              size_(std::exchange(other.size_, 0)) {}

        VirtualMemory& operator=(VirtualMemory&& other) noexcept {
            if (this != &other) {
                Free();
                ptr_ = std::exchange(other.ptr_, nullptr);
                size_ = std::exchange(other.size_, 0);
            }
            return *this;
        }

        void Free() {
            if (ptr_) {
                FreeVirtualMemory(ptr_, size_);
                ptr_ = nullptr;
                size_ = 0;
            }
        }

        [[nodiscard]] void* GetPtr() const { return ptr_; }
        [[nodiscard]] size_t GetSize() const { return size_; }

    private:
        void* ptr_ = nullptr;
        size_t size_ = 0;
    };

} // namespace Memory
