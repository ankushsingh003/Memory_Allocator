#pragma once
#include "memlab/vm/replacement/fifo.hpp"  // for IReplacement
#include <cstdint>
#include <vector>

namespace memlab {

/**
 * Clock (Second Chance) replacement.
 * Circular buffer of frames with a reference bit.
 * On victim selection: if ref bit=1, clear it and advance.
 *                      if ref bit=0, evict.
 */
class ClockReplacement : public IReplacement {
public:
    void on_load(uint32_t frame) override {
        _ring.push_back({frame, true});
        _frame_idx[frame] = _ring.size() - 1;
    }

    void on_access(uint32_t frame) override {
        auto it = _frame_idx.find(frame);
        if (it != _frame_idx.end())
            _ring[it->second].ref = true;
    }

    uint32_t victim() override {
        // Advance hand until we find ref=0
        for (size_t tries = 0; tries < _ring.size() * 2; ++tries) {
            auto& slot = _ring[_hand];
            if (!slot.ref) return slot.frame;
            slot.ref = false;
            _hand = (_hand + 1) % _ring.size();
        }
        return _ring[_hand].frame; // all referenced — evict current
    }

    void on_evict(uint32_t frame) override {
        auto it = _frame_idx.find(frame);
        if (it == _frame_idx.end()) return;
        size_t idx = it->second;
        // Remove from ring (swap with last for O(1))
        uint32_t last_frame = _ring.back().frame;
        _ring[idx] = _ring.back();
        _ring.pop_back();
        _frame_idx[last_frame] = idx;
        _frame_idx.erase(frame);
        if (_hand >= _ring.size() && !_ring.empty())
            _hand = 0;
    }

    const char* name() const override { return "Clock"; }

private:
    struct Slot { uint32_t frame; bool ref; };
    std::vector<Slot>                     _ring;
    std::unordered_map<uint32_t, size_t>  _frame_idx;
    size_t                                _hand{0};
};

} // namespace memlab
