#pragma once
#include <cstdint>
#include <queue>
#include <unordered_set>

namespace memlab {

struct IReplacement {
    virtual void     on_load(uint32_t frame)   = 0;
    virtual void     on_access(uint32_t frame) = 0;
    virtual uint32_t victim()                  = 0;
    virtual void     on_evict(uint32_t frame)  = 0;
    virtual const char* name() const           = 0;
    virtual ~IReplacement() = default;
};

/// FIFO replacement — evict the oldest loaded frame.
class FIFOReplacement : public IReplacement {
public:
    void on_load(uint32_t frame) override {
        _order.push(frame);
        _in_use.insert(frame);
    }

    void on_access(uint32_t frame) override { (void)frame; /* FIFO ignores accesses */ }

    uint32_t victim() override {
        // Skip frames that were already evicted (shouldn't happen in normal use)
        while (!_order.empty() && !_in_use.count(_order.front()))
            _order.pop();
        return _order.front();
    }

    void on_evict(uint32_t frame) override {
        _in_use.erase(frame);
    }

    const char* name() const override { return "FIFO"; }

private:
    std::queue<uint32_t>         _order;
    std::unordered_set<uint32_t> _in_use;
};

} // namespace memlab
