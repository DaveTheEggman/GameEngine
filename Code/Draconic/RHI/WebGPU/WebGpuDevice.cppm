/// draconic.rhi.webgpu:device - Device over WGPUDevice.
///
/// BRING-UP STAGE: real device/queue lifecycle (creation, loss latch, WaitIdle,
/// fences, destruction); every resource/pipeline/command factory is an HONEST
/// NotSupported until its stage lands - callers get failures, never silent fakes.
/// Build-out order (web-platform.md P1): resources -> pipelines/bind groups ->
/// encoders + swapchain (triangle) -> transfer/queries/bundles (full renderer).

module;
#include "Core/Prelude.h"
#include "WebGpuIncludes.h"

export module draconic.rhi.webgpu:device;

import draconic.core;
import draconic.rhi;
import :api;
import :fence;
import :queue;

using namespace draconic::core;

export namespace draconic::rhi::webgpu
{
    class WebGpuDevice final : public Device
    {
    public:
        WebGpuDevice(const WebGpuApi& api, WGPUInstance instance, WGPUDevice device,
                     IAllocator& allocator)
            : m_api(&api), m_instance(instance), m_device(device), m_allocator(allocator)
        {
            type = DeviceType::WebGPU;
            const WGPUQueue queue = m_api->wgpuDeviceGetQueue(m_device);
            // ONE WebGPU queue, three RHI-typed views of it (see :queue).
            m_graphicsQueue.Initialize(api, instance, queue, allocator, QueueType::Graphics);
            m_computeQueue.Initialize(api, instance, queue, allocator, QueueType::Compute);
            m_transferQueue.Initialize(api, instance, queue, allocator, QueueType::Transfer);
        }

        /// The device-lost callback (registered at creation by the adapter) lands here.
        void MarkLost() { m_lost = true; }

        [[nodiscard]] WGPUDevice Handle() const { return m_device; }
        [[nodiscard]] WGPUInstance Instance() const { return m_instance; }
        [[nodiscard]] const WebGpuApi& Api() const { return *m_api; }

        // ---- Queries ----
        Queue* GetQueue(QueueType queueType, u32 index) override
        {
            if (index != 0)
            {
                return nullptr;
            }
            switch (queueType)
            {
            case QueueType::Graphics:
                return &m_graphicsQueue;
            case QueueType::Compute:
                return &m_computeQueue;
            case QueueType::Transfer:
                return &m_transferQueue;
            }
            return nullptr;
        }

        u32 GetQueueCount(QueueType) override { return 1; }

        FormatSupport GetFormatSupport(TextureFormat) override
        {
            // Conservative until the format table lands with the texture stage.
            return FormatSupport::Texture | FormatSupport::ColorAttachment |
                   FormatSupport::DepthStencil;
        }

        // ---- Resource creation (staged; honest NotSupported until implemented) ----
        Status CreateBuffer(const BufferDesc&, Buffer*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateTexture(const TextureDesc&, Texture*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateTextureView(Texture*, const TextureViewDesc&, TextureView*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateSampler(const SamplerDesc&, Sampler*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateShaderModule(const ShaderModuleDesc&, ShaderModule*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateBindGroupLayout(const BindGroupLayoutDesc&, BindGroupLayout*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateBindGroup(const BindGroupDesc&, BindGroup*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreatePipelineLayout(const PipelineLayoutDesc&, PipelineLayout*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreatePipelineCache(const PipelineCacheDesc&, PipelineCache*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateRenderPipeline(const RenderPipelineDesc&, RenderPipeline*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateComputePipeline(const ComputePipelineDesc&, ComputePipeline*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateCommandPool(QueueType, CommandPool*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateFence(u64 initialValue, Fence*& out) override
        {
            out = m_allocator.New<WebGpuFence>(*m_api, m_instance, initialValue);
            return ErrorCode::Ok;
        }
        Status CreateQuerySet(const QuerySetDesc&, QuerySet*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }
        Status CreateSwapChain(Surface*, const SwapChainDesc&, SwapChain*& out) override
        {
            out = nullptr;
            return ErrorCode::NotSupported;
        }

        // ---- Resource destruction ----
        // Stubs tolerate the nullptr their failed Create* handed out; implemented
        // resources delete for real.
        void DestroyBuffer(Buffer*& x) override { DeleteIfAny(x); }
        void DestroyTexture(Texture*& x) override { DeleteIfAny(x); }
        void DestroyTextureView(TextureView*& x) override { DeleteIfAny(x); }
        void DestroySampler(Sampler*& x) override { DeleteIfAny(x); }
        void DestroyShaderModule(ShaderModule*& x) override { DeleteIfAny(x); }
        void DestroyBindGroupLayout(BindGroupLayout*& x) override { DeleteIfAny(x); }
        void DestroyBindGroup(BindGroup*& x) override { DeleteIfAny(x); }
        void DestroyPipelineLayout(PipelineLayout*& x) override { DeleteIfAny(x); }
        void DestroyPipelineCache(PipelineCache*& x) override { DeleteIfAny(x); }
        void DestroyRenderPipeline(RenderPipeline*& x) override { DeleteIfAny(x); }
        void DestroyComputePipeline(ComputePipeline*& x) override { DeleteIfAny(x); }
        void DestroyCommandPool(CommandPool*& x) override { DeleteIfAny(x); }
        void DestroyFence(Fence*& x) override { DeleteIfAny(x); }
        void DestroyQuerySet(QuerySet*& x) override { DeleteIfAny(x); }
        void DestroySwapChain(SwapChain*& x) override { DeleteIfAny(x); }
        void DestroySurface(Surface*& x) override { DeleteIfAny(x); }

        // ---- Lifecycle ----
        bool IsLost() override { return m_lost; }

        void WaitIdle() override
        {
            // wgpu-native's DevicePoll(wait) drains the queue; on web (no poll
            // extension) the queue wrapper's callback pump does the same job.
            if (m_api->wgpuDevicePoll != nullptr)
            {
                (void)m_api->wgpuDevicePoll(m_device, 1u, nullptr);
            }
            else
            {
                m_graphicsQueue.WaitIdle();
            }
        }

        void Destroy() override
        {
            m_api->wgpuQueueRelease(m_graphicsQueue.Handle());
            m_api->wgpuDeviceRelease(m_device);
            IAllocator& allocator = m_allocator;
            this->~WebGpuDevice();
            allocator.Free(this);
        }

    private:
        template <typename T> void DeleteIfAny(T*& object)
        {
            if (object != nullptr)
            {
                m_allocator.Delete(object);
                object = nullptr;
            }
        }

        const WebGpuApi* m_api;
        WGPUInstance m_instance;
        WGPUDevice m_device;
        IAllocator& m_allocator;
        WebGpuQueue m_graphicsQueue;
        WebGpuQueue m_computeQueue;
        WebGpuQueue m_transferQueue;
        bool m_lost = false;
    };
}
