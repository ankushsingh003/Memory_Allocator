#pragma once
#include "memlab/vm/replacement/fifo.hpp"  // for IReplacement
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <limits>
#include <algorithm>

namespace memlab {

/**
 * Optimal (Belady's MIN) replacement — offline algorithm.
 *
 * Requires the full reference trace at construction time.
 * On each victim() call, it evicts the frame whose next use is furthest
 * in the future (or never used again).
 *
 * Usage:
 *   OptimalReplacement opt(full_trace);
 *   opt.on_load(frame);     // track which frames are loaded
 *   opt.advance();          // call once per access to move the cursor
 *   opt.victim();           // returns the optimal eviction choice
 */
class OptimalReplacement : public IReplacement {
public:
    explicit OptimalReplacement(const std::vector<uint32_t>& trace)
        : _trace(trace), _pos(0)
    {
        // Pre-compute next-use index for each position
        // next_use[i] = next index j > i where trace[j] == trace[i], or INF
        _next_use.resize(_trace.size(), std::numeric_limits<size_t>::max());
        std::unordered_map<uint32_t, size_t> last;
        for (size_t i = _trace.size(); i-- > 0; ) {
            auto it = last.find(_trace[i]);
            if (it != last.end()) _next_use[i] = it->second;
            last[_trace[i]] = i;
        }
    }

    void on_load(uint32_t frame) override  { _loaded.insert(frame); }
    void on_access(uint32_t frame) override { (void)frame; }

    /// Advance the internal cursor by one reference.
    void advance() { if (_pos < _trace.size()) ++_pos; }

    uint32_t victim() override {
        // Among loaded frames, find the one with the furthest next use
        uint32_t best_frame = *_loaded.begin();
        size_t   best_dist  = 0;

        // Find next use for each loaded frame from current position
        for (uint32_t f : _loaded) {
            size_t next_use = std::numeric_limits<size_t>::max();
            for (size_t j = _pos; j < _trace.size(); ++j) {
                if (_trace[j] == f) { next_use = j; break; }
            }
            if (next_use > best_dist) {
                best_dist  = next_use;
                best_frame = f;
            }
        }
        return best_frame;
    }

    void on_evict(uint32_t frame) override { _loaded.erase(frame); }

    const char* name() const override { return "Optimal"; }

private:
    std::vector<uint32_t>          _trace;
    std::vector<size_t>            _next_use;
    size_t                         _pos;
    std::unordered_set<uint32_t>   _loaded;
};

} // namespace memlab
