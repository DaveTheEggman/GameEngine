/// Validation wrapper for Fence.
/// Ported from Sedulous.RHI.Validation/ValidatedFence.bf.

module;
#include "Core/Prelude.h"

export module draconic.rhi.validation:validated_fence;

import draconic.core;
import draconic.rhi;

using namespace foundation::core;

export namespace foundation::rhi::validation
{

    class ValidatedFence : public Fence
    {
    public:
        explicit ValidatedFence(Fence* inner) : m_inner(inner) {}

        u64 CompletedValue() override { return m_inner->CompletedValue(); }

        bool Wait(u64 value, u64 timeoutNs) override
        {
            if (value > m_lastSignaled && m_lastSignaled > 0)
            {
                LogWarningf(
                    "[Validation] Fence::wait: waiting for value %llu but highest signaled is %llu",
                    static_cast<unsigned long long>(value),
                    static_cast<unsigned long long>(m_lastSignaled));
            }
            return m_inner->Wait(value, timeoutNs);
        }

        void trackSignal(u64 value)
        {
            if (value <= m_lastSignaled && m_lastSignaled > 0)
            {
                LogWarningf("[Validation] Fence signal value %llu is not monotonically increasing "
                            "(last=%llu)",
                            static_cast<unsigned long long>(value),
                            static_cast<unsigned long long>(m_lastSignaled));
            }
            m_lastSignaled = value;
        }

        Fence* inner() const { return m_inner; }

    private:
        Fence* m_inner;
        u64 m_lastSignaled = 0;
    };

} // namespace foundation::rhi::validation
