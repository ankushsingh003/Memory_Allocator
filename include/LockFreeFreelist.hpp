#pragma once
#include "TaggedPointer.hpp"
#include <atomic>
#include <cstdint>

namespace Memory {

    /**
     * @brief An ABA-Safe Multi-Producer Multi-Consumer (MPMC) Freelist.
     * 
     * Uses 128-bit atomic Tagged Pointers to detect if a node was recycled 
     * during a pop operation, preventing the ABA problem.
     */
    template <typename T>
    class LockFreeFreelist {
    public:
        struct Node {
            std::atomic<Node*> next;
        };

        LockFreeFreelist() : head_({nullptr, 0}) {}

        /**
         * @brief Pushes a pointer back onto the list. (Lock-free + ABA safe)
         */
        void Push(void* ptr) {
            Node* node = reinterpret_cast<Node*>(ptr);
            TaggedPointer<Node> oldHead = head_.load(std::memory_order_relaxed);
            
            do {
                node->next.store(oldHead.ptr, std::memory_order_relaxed);
                
                TaggedPointer<Node> newHead = {node, oldHead.tag + 1};
                if (head_.compare_exchange_weak(oldHead, newHead, 
                                               std::memory_order_release, 
                                               std::memory_order_relaxed)) {
                    break;
                }
            } while (true);
        }

        /**
         * @brief Pops a pointer from the list. (Lock-free + ABA safe)
         * @return Pointer or nullptr if empty.
         */
        void* Pop() {
            TaggedPointer<Node> oldHead = head_.load(std::memory_order_acquire);
            
            while (oldHead.ptr) {
                Node* nextNode = oldHead.ptr->next.load(std::memory_order_relaxed);
                TaggedPointer<Node> newHead = {nextNode, oldHead.tag + 1};
                
                if (head_.compare_exchange_weak(oldHead, newHead,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                    return oldHead.ptr;
                }
                // oldHead is refreshed by compare_exchange_weak on failure.
            }
            
            return nullptr;
        }

    private:
        // Use std::atomic on the TaggedPointer struct.
        // On modern 64-bit systems, this will use CMPXCHG16B.
        alignas(16) std::atomic<TaggedPointer<Node>> head_;
    };

} // namespace Memory
