/// Validation wrapper for Device. Tracks all live resources for leak
/// detection, validates create/destroy parameters.
/// Ported from Sedulous.RHI.Validation/ValidatedDevice.bf.

module;
#include "Core/Prelude.h"

export module draconic.rhi.validation:validated_device;

import draconic.core;
import draconic.rhi;
import :validated_fence;
import :validated_swap_chain;
import :validated_command_pool;
import :validated_queue;

using namespace draconic::core;

export namespace draconic::rhi::validation
{

    class ValidatedDevice : public Device
    {
    public:
        explicit ValidatedDevice(Device* inner, IAllocator& allocator)
            : m_inner(inner), m_allocator(allocator)
        {
            type = inner->type;
            features = inner->features;
            shaderGroupHandleSize = inner->shaderGroupHandleSize;
            shaderGroupHandleAlignment = inner->shaderGroupHandleAlignment;
            shaderGroupBaseAlignment = inner->shaderGroupBaseAlignment;
        }

        // ---- Queues ----
        Queue* GetQueue(QueueType t, u32 index) override
        {
            // Wrap on first access.
            Queue* raw = m_inner->GetQueue(t, index);
            if (!raw)
                return nullptr;
            for (auto& w : m_queueWrappers)
                if (w.raw == raw)
                    return w.validated;
            auto* vq = m_allocator.New<ValidatedQueue>(raw, m_allocator);
            m_queueWrappers.PushBack({raw, vq});
            return vq;
        }
        u32 GetQueueCount(QueueType t) override { return m_inner->GetQueueCount(t); }
        FormatSupport GetFormatSupport(TextureFormat f) override
        {
            return m_inner->GetFormatSupport(f);
        }

        // ---- Create methods (with validation + tracking) ----
#define V_CREATE(Type, method, desc_t)                                                             \
    Status method(const desc_t& d, Type*& out) override                                            \
    {                                                                                              \
        if (m_destroyed)                                                                           \
        {                                                                                          \
            LogError("[Validation] " #method ": device destroyed");                                \
            out = nullptr;                                                                         \
            return ErrorCode::Unknown;                                                             \
        }                                                                                          \
        Status r = m_inner->method(d, out);                                                        \
        if (r == ErrorCode::Ok && out)                                                             \
            m_live##Type##s.PushBack(out);                                                         \
        return r;                                                                                  \
    }

        V_CREATE(Buffer, CreateBuffer, BufferDesc)
        V_CREATE(Texture, CreateTexture, TextureDesc)
        V_CREATE(Sampler, CreateSampler, SamplerDesc)
        V_CREATE(ShaderModule, CreateShaderModule, ShaderModuleDesc)
        V_CREATE(BindGroupLayout, CreateBindGroupLayout, BindGroupLayoutDesc)
        V_CREATE(BindGroup, CreateBindGroup, BindGroupDesc)
        V_CREATE(PipelineLayout, CreatePipelineLayout, PipelineLayoutDesc)
        V_CREATE(PipelineCache, CreatePipelineCache, PipelineCacheDesc)
        V_CREATE(RenderPipeline, CreateRenderPipeline, RenderPipelineDesc)
        V_CREATE(ComputePipeline, CreateComputePipeline, ComputePipelineDesc)
        V_CREATE(QuerySet, CreateQuerySet, QuerySetDesc)
#undef V_CREATE

        Status CreateTextureView(Texture* tex, const TextureViewDesc& d, TextureView*& out) override
        {
            if (m_destroyed)
            {
                LogError("[Validation] createTextureView: device destroyed");
                out = nullptr;
                return ErrorCode::Unknown;
            }
            if (!tex)
            {
                LogError("[Validation] createTextureView: texture is null");
                out = nullptr;
                return ErrorCode::Unknown;
            }
            Status r = m_inner->CreateTextureView(tex, d, out);
            if (r == ErrorCode::Ok && out)
                m_liveTextureViews.PushBack(out);
            return r;
        }

        Status CreateCommandPool(QueueType qt, CommandPool*& out) override
        {
            if (m_destroyed)
            {
                LogError("[Validation] createCommandPool: device destroyed");
                out = nullptr;
                return ErrorCode::Unknown;
            }
            CommandPool* innerPool = nullptr;
            Status r = m_inner->CreateCommandPool(qt, innerPool);
            if (r != ErrorCode::Ok || !innerPool)
            {
                out = nullptr;
                return r;
            }
            out = m_allocator.New<ValidatedCommandPool>(innerPool, m_allocator);
            m_liveCommandPools.PushBack(out);
            return ErrorCode::Ok;
        }

        Status CreateFence(u64 initialValue, Fence*& out) override
        {
            if (m_destroyed)
            {
                LogError("[Validation] createFence: device destroyed");
                out = nullptr;
                return ErrorCode::Unknown;
            }
            Fence* innerFence = nullptr;
            Status r = m_inner->CreateFence(initialValue, innerFence);
            if (r != ErrorCode::Ok || !innerFence)
            {
                out = nullptr;
                return r;
            }
            out = m_allocator.New<ValidatedFence>(innerFence);
            m_liveFences.PushBack(out);
            return ErrorCode::Ok;
        }

        Status CreateSwapChain(Surface* surface, const SwapChainDesc& d, SwapChain*& out) override
        {
            if (m_destroyed)
            {
                LogError("[Validation] createSwapChain: device destroyed");
                out = nullptr;
                return ErrorCode::Unknown;
            }
            SwapChain* innerSc = nullptr;
            Status r = m_inner->CreateSwapChain(surface, d, innerSc);
            if (r != ErrorCode::Ok || !innerSc)
            {
                out = nullptr;
                return r;
            }
            out = m_allocator.New<ValidatedSwapChain>(innerSc);
            m_liveSwapChains.PushBack(out);
            return ErrorCode::Ok;
        }

        // ---- Mesh/RT (forwarded, validated for destroyed state) ----
        Status CreateMeshPipeline(const MeshPipelineDesc& d, MeshPipeline*& out) override
        {
            if (m_destroyed)
            {
                out = nullptr;
                return ErrorCode::Unknown;
            }
            Status r = m_inner->CreateMeshPipeline(d, out);
            if (r == ErrorCode::Ok && out)
                m_liveMeshPipelines.PushBack(out);
            return r;
        }
        void DestroyMeshPipeline(MeshPipeline*& p) override
        {
            removeAndDestroy(m_liveMeshPipelines, p,
                             [&](auto*& x) { m_inner->DestroyMeshPipeline(x); });
        }

        Status CreateAccelStruct(const AccelStructDesc& d, AccelStruct*& out) override
        {
            if (m_destroyed)
            {
                out = nullptr;
                return ErrorCode::Unknown;
            }
            Status r = m_inner->CreateAccelStruct(d, out);
            if (r == ErrorCode::Ok && out)
                m_liveAccelStructs.PushBack(out);
            return r;
        }
        void DestroyAccelStruct(AccelStruct*& a) override
        {
            removeAndDestroy(m_liveAccelStructs, a,
                             [&](auto*& x) { m_inner->DestroyAccelStruct(x); });
        }

        Status CreateRayTracingPipeline(const RayTracingPipelineDesc& d,
                                        RayTracingPipeline*& out) override
        {
            if (m_destroyed)
            {
                out = nullptr;
                return ErrorCode::Unknown;
            }
            Status r = m_inner->CreateRayTracingPipeline(d, out);
            if (r == ErrorCode::Ok && out)
                m_liveRtPipelines.PushBack(out);
            return r;
        }
        void DestroyRayTracingPipeline(RayTracingPipeline*& p) override
        {
            removeAndDestroy(m_liveRtPipelines, p,
                             [&](auto*& x) { m_inner->DestroyRayTracingPipeline(x); });
        }

        Status GetShaderGroupHandles(RayTracingPipeline* p, u32 first, u32 count,
                                     Span<u8> out) override
        {
            return m_inner->GetShaderGroupHandles(p, first, count, out);
        }

        // ---- Destroy methods (with tracking removal) ----
#define V_DESTROY(Type, method, list)                                                              \
    void method(Type*& x) override                                                                 \
    {                                                                                              \
        removeAndDestroy(list, x, [&](auto*& p) { m_inner->method(p); });                          \
    }

        V_DESTROY(Buffer, DestroyBuffer, m_liveBuffers)
        V_DESTROY(Texture, DestroyTexture, m_liveTextures)
        V_DESTROY(TextureView, DestroyTextureView, m_liveTextureViews)
        V_DESTROY(Sampler, DestroySampler, m_liveSamplers)
        V_DESTROY(ShaderModule, DestroyShaderModule, m_liveShaderModules)
        V_DESTROY(BindGroupLayout, DestroyBindGroupLayout, m_liveBindGroupLayouts)
        V_DESTROY(BindGroup, DestroyBindGroup, m_liveBindGroups)
        V_DESTROY(PipelineLayout, DestroyPipelineLayout, m_livePipelineLayouts)
        V_DESTROY(PipelineCache, DestroyPipelineCache, m_livePipelineCaches)
        V_DESTROY(RenderPipeline, DestroyRenderPipeline, m_liveRenderPipelines)
        V_DESTROY(ComputePipeline, DestroyComputePipeline, m_liveComputePipelines)
        V_DESTROY(QuerySet, DestroyQuerySet, m_liveQuerySets)
#undef V_DESTROY

        void DestroyCommandPool(CommandPool*& pool) override
        {
            if (!pool)
                return;
            removeFromList(m_liveCommandPools, pool);
            auto* vp = static_cast<ValidatedCommandPool*>(pool);
            if (vp)
            {
                CommandPool* innerPool = vp->inner();
                m_inner->DestroyCommandPool(innerPool);
                m_allocator.Delete(vp);
            }
            else
                m_inner->DestroyCommandPool(pool);
            pool = nullptr;
        }

        void DestroyFence(Fence*& fence) override
        {
            if (!fence)
                return;
            removeFromList(m_liveFences, fence);
            auto* vf = static_cast<ValidatedFence*>(fence);
            if (vf)
            {
                Fence* innerFence = vf->inner();
                m_inner->DestroyFence(innerFence);
                m_allocator.Delete(vf);
            }
            else
                m_inner->DestroyFence(fence);
            fence = nullptr;
        }

        void DestroySwapChain(SwapChain*& sc) override
        {
            if (!sc)
                return;
            removeFromList(m_liveSwapChains, sc);
            auto* vs = static_cast<ValidatedSwapChain*>(sc);
            if (vs)
            {
                SwapChain* innerSc = vs->inner();
                m_inner->DestroySwapChain(innerSc);
                m_allocator.Delete(vs);
            }
            else
                m_inner->DestroySwapChain(sc);
            sc = nullptr;
        }

        void DestroySurface(Surface*& s) override { m_inner->DestroySurface(s); }

        void WaitIdle() override { m_inner->WaitIdle(); }

        void Destroy() override
        {
            if (m_destroyed)
            {
                LogError("[Validation] Device::destroy: already destroyed");
                return;
            }
            m_destroyed = true;
            reportLeaks();
            for (auto& w : m_queueWrappers)
                m_allocator.Delete(w.validated);
            m_queueWrappers.Clear();
            m_inner->Destroy();
            IAllocator& alloc = m_allocator;
            this->~ValidatedDevice();
            alloc.Free(this);
        }

    private:
        template <typename T>
        void removeFromList(Array<T*>& list, T* item)
        {
            for (usize i = 0; i < list.Size(); ++i)
            {
                if (list[i] == item)
                {
                    list.RemoveAt(i);
                    return;
                }
            }
        }

        template <typename T, typename Fn>
        void removeAndDestroy(Array<T*>& list, T*& item, Fn destroyFn)
        {
            if (!item)
                return;
            removeFromList(list, item);
            destroyFn(item);
            item = nullptr;
        }

        void reportLeaks()
        {
            auto report = [](const char* name, usize count)
            {
                if (count > 0)
                    LogWarningf("[Validation] Device destroyed with %zu live %s(s)", count, name);
            };
            report("Buffer", m_liveBuffers.Size());
            report("Texture", m_liveTextures.Size());
            report("TextureView", m_liveTextureViews.Size());
            report("Sampler", m_liveSamplers.Size());
            report("ShaderModule", m_liveShaderModules.Size());
            report("BindGroupLayout", m_liveBindGroupLayouts.Size());
            report("BindGroup", m_liveBindGroups.Size());
            report("PipelineLayout", m_livePipelineLayouts.Size());
            report("PipelineCache", m_livePipelineCaches.Size());
            report("RenderPipeline", m_liveRenderPipelines.Size());
            report("ComputePipeline", m_liveComputePipelines.Size());
            report("MeshPipeline", m_liveMeshPipelines.Size());
            report("AccelStruct", m_liveAccelStructs.Size());
            report("RayTracingPipeline", m_liveRtPipelines.Size());
            report("CommandPool", m_liveCommandPools.Size());
            report("Fence", m_liveFences.Size());
            report("SwapChain", m_liveSwapChains.Size());
            report("QuerySet", m_liveQuerySets.Size());
        }

        Device* m_inner;
        IAllocator& m_allocator;
        bool m_destroyed = false;

        struct QueueWrap
        {
            Queue* raw;
            ValidatedQueue* validated;
        };
        Array<QueueWrap> m_queueWrappers;

        Array<Buffer*> m_liveBuffers;
        Array<Texture*> m_liveTextures;
        Array<TextureView*> m_liveTextureViews;
        Array<Sampler*> m_liveSamplers;
        Array<ShaderModule*> m_liveShaderModules;
        Array<BindGroupLayout*> m_liveBindGroupLayouts;
        Array<BindGroup*> m_liveBindGroups;
        Array<PipelineLayout*> m_livePipelineLayouts;
        Array<PipelineCache*> m_livePipelineCaches;
        Array<RenderPipeline*> m_liveRenderPipelines;
        Array<ComputePipeline*> m_liveComputePipelines;
        Array<MeshPipeline*> m_liveMeshPipelines;
        Array<AccelStruct*> m_liveAccelStructs;
        Array<RayTracingPipeline*> m_liveRtPipelines;
        Array<CommandPool*> m_liveCommandPools;
        Array<Fence*> m_liveFences;
        Array<SwapChain*> m_liveSwapChains;
        Array<QuerySet*> m_liveQuerySets;
    };

} // namespace draconic::rhi::validation
