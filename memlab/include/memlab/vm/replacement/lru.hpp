#pragma once
#include "memlab/vm/replacement/fifo.hpp"  // for IReplacement
#include <cstdint>
#include <list>
#include <unordered_map>

namespace memlab {

/**
 * LRU replacement — O(1) using doubly-linked list + hash map.
 * MRU end: list.front()   LRU end: list.back()
 */
class LRUReplacement : public IReplacement {
public:
    void on_load(uint32_t frame) override {
        _list.push_front(frame);
        _map[frame] = _list.begin();
    }

    void on_access(uint32_t frame) override {
        auto it = _map.find(frame);
        if (it == _map.end()) return;
        _list.splice(_list.begin(), _list, it->second);
    }

    uint32_t victim() override {
        return _list.back(); // LRU
    }

    void on_evict(uint32_t frame) override {
        auto it = _map.find(frame);
        if (it == _map.end()) return;
        _list.erase(it->second);
        _map.erase(it);
    }

    const char* name() const override { return "LRU"; }

private:
    std::list<uint32_t>                                  _list;
    std::unordered_map<uint32_t, std::list<uint32_t>::iterator> _map;
};

} // namespace memlab
