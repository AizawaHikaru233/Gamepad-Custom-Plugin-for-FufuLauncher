#pragma once
#include <atomic>
#include <cmath>

// Native callbacks may run while UI/input modes still describe different devices.
class InputSwitchGuard {
public:
    void Begin() { depth_.fetch_add(1, std::memory_order_acq_rel); }
    void End() { depth_.fetch_sub(1, std::memory_order_acq_rel); }
    bool IsActive() const { return depth_.load(std::memory_order_acquire) != 0; }
    bool CanStart(int inputMode, int uiMode, float now, float notBefore) const {
        return !IsActive() &&
            ((inputMode == 2 && uiMode == 1) || (inputMode == 3 && uiMode == 2)) &&
            std::isfinite(now) && std::isfinite(notBefore) && now >= notBefore;
    }
private:
    std::atomic<unsigned> depth_{ 0 };
};
