/// Validation wrapper for Device. Tracks all live resources for leak
/// detection, validates create/destroy parameters.
/// Ported from Sedulous.RHI.Validation/ValidatedDevice.bf.

module;
#include "Core/Prelude.h"


export module raptor.rhi.validation:validated_device;

import raptor.core;
import raptor.rhi;
import :validated_fence;
import :validated_swap_chain;
import :validated_command_pool;
import :validated_queue;

using namespace raptor::core;

export namespace raptor::rhi::validation {

class ValidatedDevice : public Device {
public:
    explicit ValidatedDevice(Device* inner) : inner_(inner) {
        type     = inner->type;
        features = inner->features;
        shaderGroupHandleSize      = inner->shaderGroupHandleSize;
        shaderGroupHandleAlignment = inner->shaderGroupHandleAlignment;
        shaderGroupBaseAlignment   = inner->shaderGroupBaseAlignment;
    }

    // ---- Queues ----
    Queue* GetQueue(QueueType t, u32 index) override {
        // Wrap on first access.
        Queue* raw = inner_->GetQueue(t, index);
        if (!raw) return nullptr;
        for (auto& w : queueWrappers_) if (w.raw == raw) return w.validated;
        auto* vq = new ValidatedQueue(raw);
        queueWrappers_.PushBack({ raw, vq });
        return vq;
    }
    u32 GetQueueCount(QueueType t) override { return inner_->GetQueueCount(t); }
    FormatSupport GetFormatSupport(TextureFormat f) override { return inner_->GetFormatSupport(f); }

    // ---- Create methods (with validation + tracking) ----
#define V_CREATE(Type, method, desc_t) \
    Status method(const desc_t& d, Type*& out) override { \
        if (destroyed_) { LogError("[Validation] " #method ": device destroyed"); out = nullptr; return ErrorCode::Unknown; } \
        Status r = inner_->method(d, out); \
        if (r == ErrorCode::Ok && out) live##Type##s_.PushBack(out); \
        return r; \
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

    Status CreateTextureView(Texture* tex, const TextureViewDesc& d, TextureView*& out) override {
        if (destroyed_) { LogError("[Validation] createTextureView: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        if (!tex) { LogError("[Validation] createTextureView: texture is null"); out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->CreateTextureView(tex, d, out);
        if (r == ErrorCode::Ok && out) liveTextureViews_.PushBack(out);
        return r;
    }

    Status CreateCommandPool(QueueType qt, CommandPool*& out) override {
        if (destroyed_) { LogError("[Validation] createCommandPool: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        CommandPool* innerPool = nullptr;
        Status r = inner_->CreateCommandPool(qt, innerPool);
        if (r != ErrorCode::Ok || !innerPool) { out = nullptr; return r; }
        out = new ValidatedCommandPool(innerPool);
        liveCommandPools_.PushBack(out);
        return ErrorCode::Ok;
    }

    Status CreateFence(u64 initialValue, Fence*& out) override {
        if (destroyed_) { LogError("[Validation] createFence: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        Fence* innerFence = nullptr;
        Status r = inner_->CreateFence(initialValue, innerFence);
        if (r != ErrorCode::Ok || !innerFence) { out = nullptr; return r; }
        out = new ValidatedFence(innerFence);
        liveFences_.PushBack(out);
        return ErrorCode::Ok;
    }

    Status CreateSwapChain(Surface* surface, const SwapChainDesc& d, SwapChain*& out) override {
        if (destroyed_) { LogError("[Validation] createSwapChain: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        SwapChain* innerSc = nullptr;
        Status r = inner_->CreateSwapChain(surface, d, innerSc);
        if (r != ErrorCode::Ok || !innerSc) { out = nullptr; return r; }
        out = new ValidatedSwapChain(innerSc);
        liveSwapChains_.PushBack(out);
        return ErrorCode::Ok;
    }

    // ---- Mesh/RT (forwarded, validated for destroyed state) ----
    Status CreateMeshPipeline(const MeshPipelineDesc& d, MeshPipeline*& out) override {
        if (destroyed_) { out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->CreateMeshPipeline(d, out);
        if (r == ErrorCode::Ok && out) liveMeshPipelines_.PushBack(out);
        return r;
    }
    void DestroyMeshPipeline(MeshPipeline*& p) override { removeAndDestroy(liveMeshPipelines_, p, [&](auto*& x){ inner_->DestroyMeshPipeline(x); }); }

    Status CreateAccelStruct(const AccelStructDesc& d, AccelStruct*& out) override {
        if (destroyed_) { out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->CreateAccelStruct(d, out);
        if (r == ErrorCode::Ok && out) liveAccelStructs_.PushBack(out);
        return r;
    }
    void DestroyAccelStruct(AccelStruct*& a) override { removeAndDestroy(liveAccelStructs_, a, [&](auto*& x){ inner_->DestroyAccelStruct(x); }); }

    Status CreateRayTracingPipeline(const RayTracingPipelineDesc& d, RayTracingPipeline*& out) override {
        if (destroyed_) { out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->CreateRayTracingPipeline(d, out);
        if (r == ErrorCode::Ok && out) liveRtPipelines_.PushBack(out);
        return r;
    }
    void DestroyRayTracingPipeline(RayTracingPipeline*& p) override { removeAndDestroy(liveRtPipelines_, p, [&](auto*& x){ inner_->DestroyRayTracingPipeline(x); }); }

    Status GetShaderGroupHandles(RayTracingPipeline* p, u32 first, u32 count, Span<u8> out) override {
        return inner_->GetShaderGroupHandles(p, first, count, out);
    }

    // ---- Destroy methods (with tracking removal) ----
#define V_DESTROY(Type, method, list) \
    void method(Type*& x) override { removeAndDestroy(list, x, [&](auto*& p){ inner_->method(p); }); }

    V_DESTROY(Buffer, DestroyBuffer, liveBuffers_)
    V_DESTROY(Texture, DestroyTexture, liveTextures_)
    V_DESTROY(TextureView, DestroyTextureView, liveTextureViews_)
    V_DESTROY(Sampler, DestroySampler, liveSamplers_)
    V_DESTROY(ShaderModule, DestroyShaderModule, liveShaderModules_)
    V_DESTROY(BindGroupLayout, DestroyBindGroupLayout, liveBindGroupLayouts_)
    V_DESTROY(BindGroup, DestroyBindGroup, liveBindGroups_)
    V_DESTROY(PipelineLayout, DestroyPipelineLayout, livePipelineLayouts_)
    V_DESTROY(PipelineCache, DestroyPipelineCache, livePipelineCaches_)
    V_DESTROY(RenderPipeline, DestroyRenderPipeline, liveRenderPipelines_)
    V_DESTROY(ComputePipeline, DestroyComputePipeline, liveComputePipelines_)
    V_DESTROY(QuerySet, DestroyQuerySet, liveQuerySets_)
#undef V_DESTROY

    void DestroyCommandPool(CommandPool*& pool) override {
        if (!pool) return;
        removeFromList(liveCommandPools_, pool);
        auto* vp = static_cast<ValidatedCommandPool*>(pool);
        if (vp) { CommandPool* innerPool = vp->inner(); inner_->DestroyCommandPool(innerPool); delete vp; }
        else inner_->DestroyCommandPool(pool);
        pool = nullptr;
    }

    void DestroyFence(Fence*& fence) override {
        if (!fence) return;
        removeFromList(liveFences_, fence);
        auto* vf = static_cast<ValidatedFence*>(fence);
        if (vf) { Fence* innerFence = vf->inner(); inner_->DestroyFence(innerFence); delete vf; }
        else inner_->DestroyFence(fence);
        fence = nullptr;
    }

    void DestroySwapChain(SwapChain*& sc) override {
        if (!sc) return;
        removeFromList(liveSwapChains_, sc);
        auto* vs = static_cast<ValidatedSwapChain*>(sc);
        if (vs) { SwapChain* innerSc = vs->inner(); inner_->DestroySwapChain(innerSc); delete vs; }
        else inner_->DestroySwapChain(sc);
        sc = nullptr;
    }

    void DestroySurface(Surface*& s) override { inner_->DestroySurface(s); }

    void WaitIdle() override { inner_->WaitIdle(); }

    void Destroy() override {
        if (destroyed_) { LogError("[Validation] Device::destroy: already destroyed"); return; }
        destroyed_ = true;
        reportLeaks();
        for (auto& w : queueWrappers_) delete w.validated;
        queueWrappers_.Clear();
        inner_->Destroy();
        delete this;
    }

private:
    template <typename T>
    void removeFromList(Array<T*>& list, T* item) {
        for (usize i = 0; i < list.Size(); ++i) {
            if (list[i] == item) { list.RemoveAt(i); return; }
        }
    }

    template <typename T, typename Fn>
    void removeAndDestroy(Array<T*>& list, T*& item, Fn destroyFn) {
        if (!item) return;
        removeFromList(list, item);
        destroyFn(item);
        item = nullptr;
    }

    void reportLeaks() {
        auto report = [](const char* name, usize count) {
            if (count > 0) LogWarningf("[Validation] Device destroyed with %zu live %s(s)", count, name);
        };
        report("Buffer", liveBuffers_.Size());
        report("Texture", liveTextures_.Size());
        report("TextureView", liveTextureViews_.Size());
        report("Sampler", liveSamplers_.Size());
        report("ShaderModule", liveShaderModules_.Size());
        report("BindGroupLayout", liveBindGroupLayouts_.Size());
        report("BindGroup", liveBindGroups_.Size());
        report("PipelineLayout", livePipelineLayouts_.Size());
        report("PipelineCache", livePipelineCaches_.Size());
        report("RenderPipeline", liveRenderPipelines_.Size());
        report("ComputePipeline", liveComputePipelines_.Size());
        report("MeshPipeline", liveMeshPipelines_.Size());
        report("AccelStruct", liveAccelStructs_.Size());
        report("RayTracingPipeline", liveRtPipelines_.Size());
        report("CommandPool", liveCommandPools_.Size());
        report("Fence", liveFences_.Size());
        report("SwapChain", liveSwapChains_.Size());
        report("QuerySet", liveQuerySets_.Size());
    }

    Device* inner_;
    bool    destroyed_ = false;

    struct QueueWrap { Queue* raw; ValidatedQueue* validated; };
    Array<QueueWrap> queueWrappers_;

    Array<Buffer*>            liveBuffers_;
    Array<Texture*>           liveTextures_;
    Array<TextureView*>       liveTextureViews_;
    Array<Sampler*>           liveSamplers_;
    Array<ShaderModule*>      liveShaderModules_;
    Array<BindGroupLayout*>   liveBindGroupLayouts_;
    Array<BindGroup*>         liveBindGroups_;
    Array<PipelineLayout*>    livePipelineLayouts_;
    Array<PipelineCache*>     livePipelineCaches_;
    Array<RenderPipeline*>    liveRenderPipelines_;
    Array<ComputePipeline*>   liveComputePipelines_;
    Array<MeshPipeline*>      liveMeshPipelines_;
    Array<AccelStruct*>       liveAccelStructs_;
    Array<RayTracingPipeline*>liveRtPipelines_;
    Array<CommandPool*>       liveCommandPools_;
    Array<Fence*>             liveFences_;
    Array<SwapChain*>         liveSwapChains_;
    Array<QuerySet*>          liveQuerySets_;
};

} // namespace raptor::rhi::validation
