#pragma once
#include "VirtualMemory.hpp"
#include "MemoryUtils.hpp"
#include <cstdint>
#include <vector>
#include <new>

namespace Memory {

    /**
     * @brief A single Slab of memory.
     */
    template <typename T>
    struct Slab {
        struct Node {
            Node* next;
        };

        VirtualMemory storage;
        Node* freeList;
        size_t freeSlots;
        size_t totalSlots;

        Slab(size_t count, size_t slotSize) 
            : storage(count * slotSize), 
              freeSlots(count), 
              totalSlots(count) 
        {
            void* mem = storage.GetPtr();
            freeList = static_cast<Node*>(mem);
            Node* current = freeList;
            for (size_t i = 0; i < count - 1; ++i) {
                current->next = reinterpret_cast<Node*>(reinterpret_cast<uint8_t*>(current) + slotSize);
                current = current->next;
            }
            current->next = nullptr;

            // Constructor Preservation: Construct objects ONCE
            for (size_t i = 0; i < count; ++i) {
                void* objPtr = static_cast<uint8_t*>(mem) + (i * slotSize);
                new (objPtr) T(); 
            }
        }

        bool Contains(void* ptr) const {
            uint8_t* p = static_cast<uint8_t*>(ptr);
            uint8_t* start = static_cast<uint8_t*>(storage.GetPtr());
            return p >= start && p < (start + storage.GetSize());
        }
    };

    /**
     * @brief SlabCache for type T.
     */
    template <typename T>
    class SlabCache {
    public:
        explicit SlabCache(size_t objectsPerSlab = 64, size_t magazineSize = 16) 
            : objectsPerSlab_(objectsPerSlab), 
              magazineSize_(magazineSize) {
            slotSize_ = AlignUp(sizeof(T) > sizeof(typename Slab<T>::Node*) ? sizeof(T) : sizeof(typename Slab<T>::Node*), alignof(T));
        }

        T* Allocate() {
            // 1. Check Magazine (Fastest Path)
            if (!magazine_.empty()) {
                T* ptr = magazine_.back();
                magazine_.pop_back();
                return ptr;
            }

            // 2. Check slabs with free space
            for (auto& slab : slabs_) {
                if (slab.freeSlots > 0) {
                    return AllocateFromSlab(slab);
                }
            }

            // 3. Create new slab
            slabs_.emplace_back(objectsPerSlab_, slotSize_);
            return AllocateFromSlab(slabs_.back());
        }

        void Free(T* ptr) {
            if (!ptr) return;

            // 1. Try to push to Magazine (Fast Path)
            if (magazine_.size() < magazineSize_) {
                magazine_.push_back(ptr);
                return;
            }

            // 2. Return to Slab (Slow Path)
            for (auto& slab : slabs_) {
                if (slab.Contains(ptr)) {
                    typename Slab<T>::Node* node = reinterpret_cast<typename Slab<T>::Node*>(ptr);
                    node->next = slab.freeList;
                    slab.freeList = node;
                    slab.freeSlots++;
                    return;
                }
            }
        }

    private:
        T* AllocateFromSlab(Slab<T>& slab) {
            typename Slab<T>::Node* node = slab.freeList;
            slab.freeList = slab.freeList->next;
            slab.freeSlots--;
            return reinterpret_cast<T*>(node);
        }

        size_t objectsPerSlab_;
        size_t magazineSize_;
        size_t slotSize_;
        std::vector<Slab<T>> slabs_;
        std::vector<T*> magazine_;
    };

} // namespace Memory
