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
import :buffer;
import :texture;
import :texture_view;
import :sampler;
import :shader_module;
import :bind_group_layout;
import :bind_group;
import :pipeline_layout;
import :pipeline_cache;
import :render_pipeline;
import :compute_pipeline;
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

        /// Set by the adapter: whether the device carries the Immediates feature
        /// (push-constant support; pipeline layouts with ranges fail without it).
        void SetImmediatesSupported(bool supported) { m_immediatesSupported = supported; }

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

        // ---- Resource creation (encoders/pipelines still staged - see class comment) ----
        Status CreateBuffer(const BufferDesc& bufferDesc, Buffer*& out) override
        {
            return CreateResource<WebGpuBuffer>(
                out, [&](WebGpuBuffer& b)
                { return b.Initialize(*m_api, m_instance, m_device,
                                      m_graphicsQueue.Handle(), bufferDesc); });
        }
        Status CreateTexture(const TextureDesc& textureDesc, Texture*& out) override
        {
            return CreateResource<WebGpuTexture>(
                out, [&](WebGpuTexture& t) { return t.Initialize(*m_api, m_device, textureDesc); });
        }
        Status CreateTextureView(Texture* texture, const TextureViewDesc& viewDesc,
                                 TextureView*& out) override
        {
            if (texture == nullptr)
            {
                out = nullptr;
                return ErrorCode::InvalidArgument;
            }
            return CreateResource<WebGpuTextureView>(
                out, [&](WebGpuTextureView& v) { return v.Initialize(*m_api, texture, viewDesc); });
        }
        Status CreateSampler(const SamplerDesc& samplerDesc, Sampler*& out) override
        {
            return CreateResource<WebGpuSampler>(
                out, [&](WebGpuSampler& smp) { return smp.Initialize(*m_api, m_device, samplerDesc); });
        }
        Status CreateShaderModule(const ShaderModuleDesc& moduleDesc, ShaderModule*& out) override
        {
            return CreateResource<WebGpuShaderModule>(
                out, [&](WebGpuShaderModule& m) { return m.Initialize(*m_api, m_device, moduleDesc); });
        }
        Status CreateBindGroupLayout(const BindGroupLayoutDesc& layoutDesc,
                                     BindGroupLayout*& out) override
        {
            return CreateResource<WebGpuBindGroupLayout>(
                out, [&](WebGpuBindGroupLayout& l)
                { return l.Initialize(*m_api, m_device, layoutDesc); });
        }
        Status CreateBindGroup(const BindGroupDesc& groupDesc, BindGroup*& out) override
        {
            return CreateResource<WebGpuBindGroup>(
                out,
                [&](WebGpuBindGroup& g) { return g.Initialize(*m_api, m_device, groupDesc); });
        }
        Status CreatePipelineLayout(const PipelineLayoutDesc& layoutDesc,
                                    PipelineLayout*& out) override
        {
            return CreateResource<WebGpuPipelineLayout>(
                out, [&](WebGpuPipelineLayout& l)
                { return l.Initialize(*m_api, m_device, layoutDesc, m_immediatesSupported); });
        }
        Status CreatePipelineCache(const PipelineCacheDesc&, PipelineCache*& out) override
        {
            // No cache object in WebGPU; a benign empty stand-in keeps callers happy.
            out = m_allocator.New<WebGpuPipelineCache>();
            return ErrorCode::Ok;
        }
        Status CreateRenderPipeline(const RenderPipelineDesc& pipelineDesc,
                                    RenderPipeline*& out) override
        {
            return CreateResource<WebGpuRenderPipeline>(
                out, [&](WebGpuRenderPipeline& p)
                { return p.Initialize(*m_api, m_device, pipelineDesc); });
        }
        Status CreateComputePipeline(const ComputePipelineDesc& pipelineDesc,
                                     ComputePipeline*& out) override
        {
            return CreateResource<WebGpuComputePipeline>(
                out, [&](WebGpuComputePipeline& p)
                { return p.Initialize(*m_api, m_device, pipelineDesc); });
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
        // Every path tolerates the nullptr a failed Create* handed out.
        void DestroyBuffer(Buffer*& x) override { ReleaseAndDelete<WebGpuBuffer>(x); }
        void DestroyTexture(Texture*& x) override { ReleaseAndDelete<WebGpuTexture>(x); }
        void DestroyTextureView(TextureView*& x) override
        {
            ReleaseAndDelete<WebGpuTextureView>(x);
        }
        void DestroySampler(Sampler*& x) override { ReleaseAndDelete<WebGpuSampler>(x); }
        void DestroyShaderModule(ShaderModule*& x) override
        {
            ReleaseAndDelete<WebGpuShaderModule>(x);
        }
        void DestroyBindGroupLayout(BindGroupLayout*& x) override
        {
            ReleaseAndDelete<WebGpuBindGroupLayout>(x);
        }
        void DestroyBindGroup(BindGroup*& x) override { ReleaseAndDelete<WebGpuBindGroup>(x); }
        void DestroyPipelineLayout(PipelineLayout*& x) override
        {
            ReleaseAndDelete<WebGpuPipelineLayout>(x);
        }
        void DestroyPipelineCache(PipelineCache*& x) override { DeleteIfAny(x); }
        void DestroyRenderPipeline(RenderPipeline*& x) override
        {
            ReleaseAndDelete<WebGpuRenderPipeline>(x);
        }
        void DestroyComputePipeline(ComputePipeline*& x) override
        {
            ReleaseAndDelete<WebGpuComputePipeline>(x);
        }
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
        /// Allocate, run the init closure, roll back on failure - the one Create shape.
        template <typename TResource, typename TBase, typename TInit>
        Status CreateResource(TBase*& out, TInit&& initialize)
        {
            out = nullptr;
            auto* resource = m_allocator.New<TResource>();
            const Status status = initialize(*resource);
            if (!status.IsOk())
            {
                m_allocator.Delete(resource);
                return status;
            }
            out = resource;
            return ErrorCode::Ok;
        }

        template <typename TResource, typename TBase> void ReleaseAndDelete(TBase*& object)
        {
            if (object != nullptr)
            {
                auto* resource = static_cast<TResource*>(object);
                resource->Release();
                m_allocator.Delete(resource);
                object = nullptr;
            }
        }

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
        bool m_immediatesSupported = false;
    };
}
