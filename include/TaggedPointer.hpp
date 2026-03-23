#pragma once
#include <atomic>
#include <cstdint>

namespace Memory {

    /**
     * @brief A 128-bit Tagged Pointer to solve the ABA problem in lock-free lists.
     * 
     * Bundles a 64-bit pointer with a 64-bit version tag.
     */
    template <typename T>
    struct TaggedPointer {
        T* ptr;
        std::uintptr_t tag;

        bool operator==(const TaggedPointer& other) const {
            return ptr == other.ptr && tag == other.tag;
        }
    };

} // namespace Memory
