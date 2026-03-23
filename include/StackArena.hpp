#pragma once
#include "MemoryUtils.hpp"
#include <cstdint>
#include <cstddef>

namespace Memory {

    /**
     * @brief A stack-allocated Linear Arena for ultra-fast, local allocations.
     * 
     * This allocator uses a fixed-size buffer on the STACK. It is the fastest 
     * possible allocation method but is limited to small sizes.
     * 
     * @tparam Size The size of the stack buffer in bytes.
     */
    template <std::size_t Size>
    class StackArena {
    public:
        StackArena() : offset_(0) {}

        /**
         * @brief Allocates memory from the stack buffer.
         * @param size Size in bytes.
         * @param alignment Required alignment.
         * @return Pointer to memory, or nullptr if out of stack space.
         */
        void* Allocate(std::size_t size, std::size_t alignment = 8) {
            void* currentPtr = buffer_ + offset_;
            void* alignedPtr = AlignPtr(currentPtr, alignment);

            std::size_t newOffset = static_cast<std::uint8_t*>(alignedPtr) - buffer_ + size;

            if (newOffset > Size) {
                return nullptr; // Out of stack space
            }

            offset_ = newOffset;
            return alignedPtr;
        }

        /**
         * @brief Resets the stack arena.
         */
        void Reset() {
            offset_ = 0;
        }

        [[nodiscard]] std::size_t GetUsedMemory() const { return offset_; }
        [[nodiscard]] std::size_t GetCapacity() const { return Size; }

    private:
        // The actual stack buffer
        alignas(64) std::uint8_t buffer_[Size]; 
        std::size_t offset_;
    };

} // namespace Memory
