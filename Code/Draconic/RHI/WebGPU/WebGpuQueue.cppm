/// draconic.rhi.webgpu:queue - Queue over the device's single WGPUQueue.
///
/// WebGPU exposes exactly ONE queue per device. The RHI models Graphics/Compute/
/// Transfer queues, so the device hands out three thin wrappers that all funnel into
/// the same WGPUQueue - correct by construction (one timeline) and shape-compatible
/// with callers that ask for a specific queue type. Fence signaling rides
/// wgpuQueueOnSubmittedWorkDone (see :fence for the timeline emulation).

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:queue;

import draconic.core;
import draconic.rhi;
import :api;
import :fence;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuQueue final : public Queue
    {
    public:
        WebGpuQueue() = default;

        void Initialize(const WebGpuApi& api, WGPUInstance instance, WGPUQueue queue,
                        IAllocator& allocator, QueueType type)
        {
            m_api = &api;
            m_instance = instance;
            m_queue = queue;
            m_allocator = &allocator;
            queueType = type;
        }

        [[nodiscard]] WGPUQueue Handle() const { return m_queue; }

        void Submit(Span<CommandBuffer* const> commandBuffers) override
        {
            SubmitInternal(commandBuffers);
        }

        void Submit(Span<CommandBuffer* const> commandBuffers, Fence* signalFence,
                    u64 signalValue) override
        {
            SubmitInternal(commandBuffers);
            SignalOnDone(signalFence, signalValue);
        }

        void Submit(Span<CommandBuffer* const> commandBuffers, Span<Fence* const> waitFences,
                    Span<const u64> waitValues, Fence* signalFence, u64 signalValue) override
        {
            // One queue, one timeline: anything previously submitted here is already
            // ordered before this submission, so same-queue waits are satisfied by
            // construction. Cross-queue waits cannot exist (there is only one queue) -
            // waiting CPU-side would deadlock nothing but adds latency; the values are
            // still honored for fences signaled by earlier submissions.
            for (usize i = 0; i < waitFences.Size() && i < waitValues.Size(); ++i)
            {
                if (waitFences[i] != nullptr)
                {
                    static_cast<WebGpuFence*>(waitFences[i])->Wait(waitValues[i], 0);
                }
            }
            SubmitInternal(commandBuffers);
            SignalOnDone(signalFence, signalValue);
        }

        void WaitIdle() override
        {
            bool done = false;
            WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
            info.mode = WGPUCallbackMode_AllowProcessEvents;
            info.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* userdata1, void*)
            { *static_cast<bool*>(userdata1) = true; };
            info.userdata1 = &done;
            (void)m_api->wgpuQueueOnSubmittedWorkDone(m_queue, info);
            m_api->PumpUntil(m_instance, done);
        }

        Status CreateTransferBatch(TransferBatch*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported; // arrives with the resource stage
        }

        void DestroyTransferBatch(TransferBatch*&) override {}

        f32 TimestampPeriod() const override
        {
            return m_api->wgpuQueueGetTimestampPeriod != nullptr
                       ? m_api->wgpuQueueGetTimestampPeriod(m_queue)
                       : 1.0f;
        }

    private:
        void SubmitInternal(Span<CommandBuffer* const> commandBuffers)
        {
            // Command recording is not implemented yet (encoder stage) - the only legal
            // submission today is the empty one used for fence signaling.
            DRACONIC_ASSERT_MSG(commandBuffers.IsEmpty(),
                                "webgpu: command buffers not implemented yet");
            (void)commandBuffers;
        }

        void SignalOnDone(Fence* fence, u64 value)
        {
            if (fence == nullptr)
            {
                return;
            }
            struct Pending
            {
                WebGpuFence* fence;
                u64 value;
                IAllocator* allocator;
            };
            auto* pending = m_allocator->New<Pending>();
            pending->fence = static_cast<WebGpuFence*>(fence);
            pending->value = value;
            pending->allocator = m_allocator;

            WGPUQueueWorkDoneCallbackInfo info = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
            info.mode = WGPUCallbackMode_AllowProcessEvents;
            info.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* userdata1, void*)
            {
                auto* p = static_cast<Pending*>(userdata1);
                p->fence->SignalFromCallback(p->value);
                p->allocator->Delete(p);
            };
            info.userdata1 = pending;
            (void)m_api->wgpuQueueOnSubmittedWorkDone(m_queue, info);
        }

        const WebGpuApi* m_api = nullptr;
        WGPUInstance m_instance = nullptr;
        WGPUQueue m_queue = nullptr;
        IAllocator* m_allocator = nullptr;
    };
}
