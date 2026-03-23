#pragma once
#include <atomic>
#include <cstdint>

namespace Memory {

    /**
     * @brief A Lock-Free Multi-Producer Multi-Consumer (MPMC) Freelist.
     * 
     * Uses atomic Compare-And-Swap (CAS) to manage the list head without mutexes.
     * Note: This basic version is susceptible to the ABA problem. 
     * Tagged pointers will be added in the next milestone.
     */
    template <typename T>
    class LockFreeFreelist {
    public:
        struct Node {
            std::atomic<Node*> next;
        };

        LockFreeFreelist() : head_(nullptr) {}

        /**
         * @brief Pushe a pointer back onto the list. (Lock-free)
         */
        void Push(void* ptr) {
            Node* node = reinterpret_cast<Node*>(ptr);
            Node* oldHead = head_.load(std::memory_order_relaxed);
            
            do {
                node->next.store(oldHead, std::memory_order_relaxed);
            } while (!head_.compare_exchange_weak(oldHead, node, 
                                                std::memory_order_release, 
                                                std::memory_order_relaxed));
        }

        /**
         * @brief Pops a pointer from the list. (Lock-free)
         * @return Pointer or nullptr if empty.
         */
        void* Pop() {
            Node* oldHead = head_.load(std::memory_order_acquire);
            
            while (oldHead && !head_.compare_exchange_weak(oldHead, 
                                                         oldHead->next.load(std::memory_order_relaxed),
                                                         std::memory_order_acquire,
                                                         std::memory_order_relaxed)) {
                // CAS failed, oldHead is updated with the new current head, try again.
            }
            
            return oldHead;
        }

    private:
        std::atomic<Node*> head_;
    };

} // namespace Memory
