#pragma once
#include "BuddyAllocator.hpp"
#include <memory>
#include <limits>

namespace Memory {

    // A global buddy allocator for the general-purpose wrapper.
    // In a real system, this would be initialized once.
    inline BuddyAllocator& GetGlobalBuddy() {
        static BuddyAllocator globalBuddy(1024 * 1024 * 16); // 16MB default
        return globalBuddy;
    }

    /**
     * @brief A standard-compliant C++ Allocator wrapper for BuddyAllocator.
     * 
     * Allows our custom memory management to be used with std::vector, std::list, etc.
     */
    template <typename T>
    class Allocator {
    public:
        using value_type = T;

        Allocator() noexcept = default;
        template <typename U>
        Allocator(const Allocator<U>&) noexcept {}

        T* allocate(std::size_t n) {
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
                throw std::bad_array_new_length();

            void* ptr = GetGlobalBuddy().Allocate(n * sizeof(T));
            if (!ptr) throw std::bad_alloc();
            
            return static_cast<T*>(ptr);
        }

        void deallocate(T* p, std::size_t n) noexcept {
            GetGlobalBuddy().Free(const_cast<void*>(static_cast<const void*>(p)), n * sizeof(T));
        }

        template <typename U>
        bool operator==(const Allocator<U>&) const noexcept { return true; }
        
        template <typename U>
        bool operator!=(const Allocator<U>&) const noexcept { return false; }
    };

} // namespace Memory
