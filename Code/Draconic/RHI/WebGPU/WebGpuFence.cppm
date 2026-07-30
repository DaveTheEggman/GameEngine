/// draconic.rhi.webgpu:fence - CPU-side timeline fence.
///
/// WebGPU has no fence object. The RHI's timeline contract is emulated CPU-side:
/// Queue::Submit(..., fence, value) registers a wgpuQueueOnSubmittedWorkDone
/// callback that stores `value` here when the GPU passes that submission, and
/// Wait() pumps ProcessEvents until the value arrives - single-threaded-safe,
/// which is exactly the web v1 model.

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:fence;

import draconic.core;
import draconic.rhi;
import :api;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    /// CompletedValue only advances when callbacks get delivered - Wait() pumps;
    /// passive observers see new values after any pump on the same instance.
    class WebGpuFence final : public Fence
    {
    public:
        WebGpuFence(const WebGpuApi& api, WGPUInstance instance, u64 initialValue)
            : m_api(&api), m_instance(instance), m_completed(initialValue)
        {
        }

        u64 CompletedValue() override { return m_completed; }

        bool Wait(u64 value, u64 /*timeoutNs*/) override
        {
            // The pump delivers AllowProcessEvents callbacks; the iteration guard is the
            // timeout stand-in (wgpu-native's timed waits are unimplemented - see :api).
            for (u32 i = 0; i < 1000000 && m_completed < value; ++i)
            {
                m_api->wgpuInstanceProcessEvents(m_instance);
            }
            return m_completed >= value;
        }

        void SignalFromCallback(u64 value)
        {
            if (value > m_completed)
            {
                m_completed = value;
            }
        }

    private:
        const WebGpuApi* m_api;
        WGPUInstance m_instance;
        u64 m_completed;
    };
}
