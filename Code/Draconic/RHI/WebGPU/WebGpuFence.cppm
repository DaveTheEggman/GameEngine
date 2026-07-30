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
        WebGpuFence(const WebGpuApi& api, WGPUInstance instance, WGPUDevice device,
                    u64 initialValue)
            : m_api(&api), m_instance(instance), m_device(device), m_completed(initialValue)
        {
        }

        u64 CompletedValue() override { return m_completed; }

        /// The queue records each fenced submission's wgpu SUBMISSION INDEX here.
        /// Wait() prefers waiting on that exact index - DevicePoll(wait, &index)
        /// returns when THAT submission retires, immune to unrelated queue state
        /// (a presented frame in flight can starve blanket polls on Wayland/FIFO).
        void NoteSubmission(u64 value, WGPUSubmissionIndex submissionIndex)
        {
            // Monotonic values: the latest note supersedes for lower values too.
            m_notedValue = value;
            m_notedIndex = submissionIndex;
            m_hasNote = true;
        }

        bool Wait(u64 value, u64 /*timeoutNs*/) override
        {
            if (m_completed >= value)
            {
                return true;
            }
            if (m_hasNote && m_notedValue >= value && m_api->wgpuDevicePoll != nullptr &&
                m_device != nullptr)
            {
                (void)m_api->wgpuDevicePoll(m_device, 1u, &m_notedIndex);
                SignalFromCallback(m_notedValue);
                m_hasNote = false;
                return true;
            }
            // Work-done callbacks fire on DEVICE polls. Polls are NON-blocking
            // (wait=1 can block forever on an empty queue - see :api); the iteration
            // guard is the timeout stand-in (wgpu-native's timed waits are
            // unimplemented).
            for (u32 i = 0; i < 1000000 && m_completed < value; ++i)
            {
                m_api->wgpuInstanceProcessEvents(m_instance);
                if (m_completed >= value)
                {
                    break;
                }
                if (m_api->wgpuDevicePoll != nullptr && m_device != nullptr)
                {
                    (void)m_api->wgpuDevicePoll(m_device, 0u, nullptr);
                }
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
        WGPUDevice m_device;
        u64 m_completed;
        u64 m_notedValue = 0;
        WGPUSubmissionIndex m_notedIndex{};
        bool m_hasNote = false;
    };
}
