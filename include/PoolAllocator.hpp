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
    template <typename T>
    class PoolAllocator {
    public:
        /**
         * @brief Construct the pool and pre-allocate memory from the OS.
         * @param numSlots Total number of objects the pool can hold.
         */
        explicit PoolAllocator(size_t numSlots) 
            : numSlots_(numSlots), 
              storage_(numSlots * sizeof(Node)) 
        {
            // Initialize the freelist by linking all nodes together
            Node* current = static_cast<Node*>(storage_.GetPtr());
            freeList_ = current;

            for (size_t i = 0; i < numSlots_ - 1; ++i) {
                current->next = reinterpret_cast<Node*>(reinterpret_cast<uint8_t*>(current) + sizeof(Node));
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
                return nullptr; // Pool is out of memory
            }

            // Pop the first node from the freelist
            Node* node = freeList_;
            freeList_ = freeList_->next;

            // Return the memory cast to type T
            return reinterpret_cast<T*>(node);
        }

        /**
         * @brief Returns a pointer to the pool's free list.
         * @param ptr Pointer to the memory to return to the pool.
         */
        void Free(void* ptr) {
            if (!ptr) return;

            // Push the pointer back onto the front of the freelist (LIFO)
            Node* node = reinterpret_cast<Node*>(ptr);
            node->next = freeList_;
            freeList_ = node;
        }

        [[nodiscard]] size_t GetCapacity() const { return numSlots_; }

    private:
        /**
         * @brief A node in our freelist. 
         * 
         * We use a 'union-like' trick: when the memory is free, it stores a pointer 
         * to the next free node. When it's allocated, it stores the actual object T.
         */
        struct Node {
            Node* next;
        };

        // Ensure each slot is large enough to hold either a T or our 'next' pointer
        static_assert(sizeof(T) >= sizeof(Node*), "Type T must be at least pointer-sized for the freelist trick.");

        size_t numSlots_;
        VirtualMemory storage_; // RAII OS memory
        Node* freeList_ = nullptr;
    };

} // namespace Memory
