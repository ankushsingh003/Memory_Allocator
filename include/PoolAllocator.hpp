#pragma once
#include "VirtualMemory.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>

namespace Memory {

    /**
     * @brief A high-performance Pool Allocator for objects of type T.
     * 
     * Pre-allocates a fixed number of slots. All allocations and deallocations are O(1).
     * Ideal for many small, same-sized objects (e.g., entity components, network packets).
     */
    /**
     * @brief A high-performance Pool Allocator for objects of type T.
     * 
     * @tparam T The type of object to allocate.
     * @tparam Alignment The alignment requirement in bytes (must be power of 2).
     */
    template <typename T, size_t Alignment = alignof(T)>
    class PoolAllocator {
    public:
        /**
         * @brief Construct the pool and pre-allocate memory from the OS.
         * @param numSlots Total number of objects the pool can hold.
         */
        explicit PoolAllocator(size_t numSlots) 
            : numSlots_(numSlots), 
              storage_(numSlots * SlotSize) 
        {
            // Initialize the freelist by linking all nodes together
            Node* current = static_cast<Node*>(storage_.GetPtr());
            freeList_ = current;

            for (size_t i = 0; i < numSlots_ - 1; ++i) {
                // Advance exactly one 'SlotSize' in memory
                current->next = reinterpret_cast<Node*>(reinterpret_cast<uint8_t*>(current) + SlotSize);
                current = current->next;
            }
            current->next = nullptr; // End of the list
        }

        /**
         * @brief Allocates one object of type T from the pool.
         * @return Pointer to the allocated memory, or nullptr if the pool is full.
         */
        T* Allocate() {
            if (!freeList_) {
                return nullptr;
            }

            Node* node = freeList_;
            freeList_ = freeList_->next;

            return reinterpret_cast<T*>(node);
        }

        /**
         * @brief Returns a pointer to the pool's free list.
         * @param ptr Pointer to the memory to return to the pool.
         */
        void Free(void* ptr) {
            if (!ptr) return;

            Node* node = reinterpret_cast<Node*>(ptr);
            node->next = freeList_;
            freeList_ = node;
        }

        [[nodiscard]] size_t GetCapacity() const { return numSlots_; }

    private:
        struct Node {
            Node* next;
        };

        // Important: each slot must be large enough for T AND large enough for a Node*
        // AND it must be a multiple of the requested Alignment.
        static constexpr size_t SlotSize = AlignUp(sizeof(T) > sizeof(Node*) ? sizeof(T) : sizeof(Node*), Alignment);

        size_t numSlots_;
        VirtualMemory storage_;
        Node* freeList_ = nullptr;
    };

} // namespace Memory
