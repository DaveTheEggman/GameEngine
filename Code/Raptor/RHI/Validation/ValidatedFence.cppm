/// Validation wrapper for Fence.
/// Ported from Sedulous.RHI.Validation/ValidatedFence.bf.

module;
#include "Core/Prelude.h"

export module raptor.rhi.validation:validated_fence;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedFence : public Fence {
public:
    explicit ValidatedFence(Fence* inner) : inner_(inner) {}

    u64 CompletedValue() override { return inner_->CompletedValue(); }

    bool Wait(u64 value, u64 timeoutNs) override {
        if (value > lastSignaled_ && lastSignaled_ > 0) {
            LogWarningf("[Validation] Fence::wait: waiting for value %llu but highest signaled is %llu",
                        static_cast<unsigned long long>(value), static_cast<unsigned long long>(lastSignaled_));
        }
        return inner_->Wait(value, timeoutNs);
    }

    void trackSignal(u64 value) {
        if (value <= lastSignaled_ && lastSignaled_ > 0) {
            LogWarningf("[Validation] Fence signal value %llu is not monotonically increasing (last=%llu)",
                        static_cast<unsigned long long>(value), static_cast<unsigned long long>(lastSignaled_));
        }
        lastSignaled_ = value;
    }

    Fence* inner() const { return inner_; }

private:
    Fence* inner_;
    u64    lastSignaled_ = 0;
};

} // namespace raptor::rhi::validation
