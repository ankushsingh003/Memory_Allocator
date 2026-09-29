#pragma once
#include <cstdint>
#include <unordered_map>
#include <optional>

namespace memlab {

/**
 * TLB — fully-associative, fixed-capacity TLB.
 * Eviction policy: simple LRU via a usage counter (stamp).
 *
 * Key: (pid, page_number)   Value: frame_number
 */
class TLB {
public:
    explicit TLB(uint32_t capacity = 64) : _cap(capacity) {}

    struct Entry { uint32_t frame; uint64_t stamp; };
    using Key = std::pair<int, uint64_t>;   // (pid, vpn)

    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<int>{}(k.first) ^ (std::hash<uint64_t>{}(k.second) << 32);
        }
    };

    /// Lookup. Returns frame if hit, empty optional if miss.
    std::optional<uint32_t> lookup(int pid, uint64_t vpn) {
        auto it = _map.find({pid, vpn});
        if (it == _map.end()) return std::nullopt;
        it->second.stamp = ++_clock;
        ++_hits;
        return it->second.frame;
    }

    /// Insert / update a mapping.
    void insert(int pid, uint64_t vpn, uint32_t frame) {
        Key k{pid, vpn};
        if (_map.count(k)) { _map[k] = {frame, ++_clock}; return; }
        if (_map.size() >= _cap) evict();
        _map[k] = {frame, ++_clock};
    }

    /// Invalidate a specific mapping (on eviction / process exit).
    void invalidate(int pid, uint64_t vpn) { _map.erase({pid, vpn}); }

    /// Flush all entries for a given pid.
    void flush_pid(int pid) {
        for (auto it = _map.begin(); it != _map.end(); ) {
            if (it->first.first == pid) it = _map.erase(it);
            else ++it;
        }
    }

    uint64_t hits()   const { return _hits; }
    uint64_t misses() const { return _misses; }
    void     count_miss() { ++_misses; }

    void reset_stats() { _hits = 0; _misses = 0; }

private:
    uint32_t _cap;
    uint64_t _clock{0};
    uint64_t _hits{0}, _misses{0};
    std::unordered_map<Key, Entry, KeyHash> _map;

    void evict() {
        // Find LRU entry
        auto oldest = _map.begin();
        for (auto it = _map.begin(); it != _map.end(); ++it)
            if (it->second.stamp < oldest->second.stamp) oldest = it;
        _map.erase(oldest);
    }
};

} // namespace memlab
