#pragma once
#include "VirtualMemory.hpp"
#include "MemoryUtils.hpp"
#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <mutex>

namespace Memory {

    /**
     * @brief A Buddy Allocator for variable-sized memory management.
     * 
     * Handles blocks in power-of-two sizes. Splits large blocks into buddies
     * and merges them back when freed.
     */
    class BuddyAllocator {
    public:
        /**
         * @param totalSize Total memory to manage (must be power of 2).
         * @param minBlockSize Minimum allocation unit (must be power of 2).
         */
        BuddyAllocator(size_t totalSize, size_t minBlockSize = 64) 
            : storage_(totalSize), 
              minBlockSize_(minBlockSize) 
        {
            maxOrder_ = static_cast<size_t>(std::log2(totalSize / minBlockSize));
            freeLists_.resize(maxOrder_ + 1, nullptr);
            
            // Initial block is the whole memory at the highest order
            void* base = storage_.GetPtr();
            PushNode(maxOrder_, base);
        }

        void* Allocate(size_t size) {
            size_t order = GetOrder(size);
            if (order > maxOrder_) return nullptr;

            std::lock_guard<std::mutex> lock(mutex_);
            return AllocateInternal(order);
        }

        void Free(void* ptr, size_t size) {
            if (!ptr) return;
            size_t order = GetOrder(size);
            
            std::lock_guard<std::mutex> lock(mutex_);
            FreeInternal(ptr, order);
        }

    private:
        struct Node {
            Node* next;
        };

        size_t GetOrder(size_t size) const {
            if (size <= minBlockSize_) return 0;
            return static_cast<size_t>(std::ceil(std::log2(static_cast<double>(size) / minBlockSize_)));
        }

        void* AllocateInternal(size_t order) {
            if (order > maxOrder_) return nullptr;

            // If a block of this order is available, take it
            if (freeLists_[order]) {
                return PopNode(order);
            }

            // Otherwise, split a larger block
            void* block = AllocateInternal(order + 1);
            if (block) {
                // Split: keep one half, add the other to the freelist
                size_t blockSize = minBlockSize_ << order;
                void* buddy = static_cast<uint8_t*>(block) + blockSize;
                PushNode(order, buddy);
                return block;
            }

            return nullptr;
        }

        void FreeInternal(void* ptr, size_t order) {
            if (order >= maxOrder_) {
                PushNode(maxOrder_, ptr);
                return;
            }

            // Calculate buddy address
            size_t blockSize = minBlockSize_ << order;
            uintptr_t offset = reinterpret_cast<uint8_t*>(ptr) - static_cast<uint8_t*>(storage_.GetPtr());
            uintptr_t buddyOffset = offset ^ blockSize;
            void* buddy = static_cast<uint8_t*>(storage_.GetPtr()) + buddyOffset;

            // Try to find buddy in the freelist to coalesce
            if (RemoveNodeIfPresent(order, buddy)) {
                // Coalesce: find the lower address of the pair and move to next order
                void* merged = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) & ~blockSize);
                FreeInternal(merged, order + 1);
            } else {
                PushNode(order, ptr);
            }
        }

        void PushNode(size_t order, void* ptr) {
            Node* node = static_cast<Node*>(ptr);
            node->next = freeLists_[order];
            freeLists_[order] = node;
        }

        void* PopNode(size_t order) {
            Node* node = freeLists_[order];
            freeLists_[order] = node->next;
            return node;
        }

        bool RemoveNodeIfPresent(size_t order, void* target) {
            Node** curr = &freeLists_[order];
            while (*curr) {
                if (*curr == target) {
                    *curr = (*curr)->next;
                    return true;
                }
                curr = &((*curr)->next);
            }
            return false;
        }

        VirtualMemory storage_;
        size_t minBlockSize_;
        size_t maxOrder_;
        std::vector<Node*> freeLists_;
        std::mutex mutex_;
    };

} // namespace Memory
