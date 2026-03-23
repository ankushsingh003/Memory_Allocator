#include "VirtualMemory.hpp"
#include <cassert>
#include <cstdint>
#include <cstddef>

namespace Memory {

    /**
     * @brief A Linear (Bump) Allocator for high-speed, lifetime-scoped allocations.
     * 
     * It simply "bumps" a pointer for each allocation. Individual 'Free' is not supported.
     * The entire memory block is reclaimed at once via Reset().
     */
    class LinearArena {
    public:
        /**
         * @brief Construct the arena and reserve a block of virtual memory.
         * @param totalSize Total size of the arena in bytes.
         */
        explicit LinearArena(size_t totalSize) 
            : storage_(totalSize), 
              offset_(0) {}

        /**
         * @brief Allocates a block of memory from the arena.
         * @param size Size in bytes.
         * @param alignment Required alignment (power of 2).
         * @return Pointer to allocated memory, or nullptr if out of space.
         */
        void* Allocate(size_t size, size_t alignment = 8) {
            std::uint8_t* base = static_cast<std::uint8_t*>(storage_.GetPtr());
            void* currentPtr = base + offset_;
            
            // Calculate the aligned starting point
            void* alignedPtr = AlignPtr(currentPtr, alignment);
            size_t newOffset = static_cast<std::uint8_t*>(alignedPtr) - base + size;

            if (newOffset > storage_.GetSize()) {
                return nullptr; // Out of memory
            }

            offset_ = newOffset;
            return alignedPtr;
        }

        /**
         * @brief Reset the arena to zero, effectively 'freeing' everything.
         */
        void Reset() {
            offset_ = 0;
        }

        /**
         * @brief Returns the current memory usage in bytes.
         */
        [[nodiscard]] size_t GetUsedMemory() const { return offset_; }
        [[nodiscard]] size_t GetCapacity() const { return storage_.GetSize(); }

    private:
        VirtualMemory storage_;
        size_t offset_;
    };

    /**
     * @brief RAII Scope Guard for LinearArena.
     * 
     * Automatically calls Reset() on the arena when the guard goes out of scope.
     */
    class ArenaScope {
    public:
        explicit ArenaScope(LinearArena& arena) : arena_(arena) {}
        ~ArenaScope() { arena_.Reset(); }

        // Disable copying
        ArenaScope(const ArenaScope&) = delete;
        ArenaScope& operator=(const ArenaScope&) = delete;

    private:
        LinearArena& arena_;
    };

} // namespace Memory
