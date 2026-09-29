#pragma once
#include <cstdint>
#include <queue>
#include <stdexcept>

namespace memlab {

/**
 * FrameAllocator — manages a pool of physical page frames.
 * Maintains a free-list and a count of total/used frames.
 */
class FrameAllocator {
public:
    explicit FrameAllocator(uint32_t total_frames) : _total(total_frames) {
        for (uint32_t f = 0; f < total_frames; ++f)
            _free.push(f);
    }

    /// Allocate one frame. Returns frame number, or throws if exhausted.
    uint32_t alloc() {
        if (_free.empty())
            throw std::bad_alloc();
        uint32_t f = _free.front(); _free.pop();
        return f;
    }

    /// Return a frame to the free pool.
    void free(uint32_t frame) { _free.push(frame); }

    bool     has_free()     const { return !_free.empty(); }
    uint32_t total_frames() const { return _total; }
    uint32_t free_frames()  const { return static_cast<uint32_t>(_free.size()); }
    uint32_t used_frames()  const { return _total - free_frames(); }

private:
    uint32_t            _total;
    std::queue<uint32_t> _free;
};

} // namespace memlab
