#pragma once
#include <cstdint>
#include <unordered_map>

namespace memlab {

/// One page table entry.
struct PTE {
    uint32_t frame{0};
    bool     present{false};
    bool     dirty{false};
    bool     referenced{false};
};

/**
 * PageTable — per-process, flat (one-level) page table.
 * Stored as a hash map so virtual address space can be sparse.
 */
class PageTable {
public:
    PTE* lookup(uint64_t vpn) {
        auto it = _table.find(vpn);
        return (it != _table.end()) ? &it->second : nullptr;
    }

    PTE& get_or_create(uint64_t vpn) { return _table[vpn]; }

    void invalidate(uint64_t vpn) { _table.erase(vpn); }

    void clear() { _table.clear(); }

private:
    std::unordered_map<uint64_t, PTE> _table;
};

} // namespace memlab
