/// Validation wrapper for Device. Tracks all live resources for leak
/// detection, validates create/destroy parameters.
/// Ported from Sedulous.RHI.Validation/ValidatedDevice.bf.

module;

#include <vector>

export module raptor.rhi.validation:validated_device;

import raptor.core;
import raptor.rhi;
import :validated_fence;
import :validated_swap_chain;
import :validated_command_pool;
import :validated_queue;

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
    Queue* getQueue(QueueType t, u32 index) override {
        // Wrap on first access.
        Queue* raw = inner_->getQueue(t, index);
        if (!raw) return nullptr;
        for (auto& w : queueWrappers_) if (w.raw == raw) return w.validated;
        auto* vq = new ValidatedQueue(raw);
        queueWrappers_.push_back({ raw, vq });
        return vq;
    }
    u32 getQueueCount(QueueType t) override { return inner_->getQueueCount(t); }
    FormatSupport getFormatSupport(TextureFormat f) override { return inner_->getFormatSupport(f); }

    // ---- Create methods (with validation + tracking) ----
#define V_CREATE(Type, method, desc_t) \
    Status method(const desc_t& d, Type*& out) override { \
        if (destroyed_) { logError("[Validation] " #method ": device destroyed"); out = nullptr; return ErrorCode::Unknown; } \
        Status r = inner_->method(d, out); \
        if (r == ErrorCode::Ok && out) live##Type##s_.push_back(out); \
        return r; \
    }

    V_CREATE(Buffer, createBuffer, BufferDesc)
    V_CREATE(Texture, createTexture, TextureDesc)
    V_CREATE(Sampler, createSampler, SamplerDesc)
    V_CREATE(ShaderModule, createShaderModule, ShaderModuleDesc)
    V_CREATE(BindGroupLayout, createBindGroupLayout, BindGroupLayoutDesc)
    V_CREATE(BindGroup, createBindGroup, BindGroupDesc)
    V_CREATE(PipelineLayout, createPipelineLayout, PipelineLayoutDesc)
    V_CREATE(PipelineCache, createPipelineCache, PipelineCacheDesc)
    V_CREATE(RenderPipeline, createRenderPipeline, RenderPipelineDesc)
    V_CREATE(ComputePipeline, createComputePipeline, ComputePipelineDesc)
    V_CREATE(QuerySet, createQuerySet, QuerySetDesc)
#undef V_CREATE

    Status createTextureView(Texture* tex, const TextureViewDesc& d, TextureView*& out) override {
        if (destroyed_) { logError("[Validation] createTextureView: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        if (!tex) { logError("[Validation] createTextureView: texture is null"); out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->createTextureView(tex, d, out);
        if (r == ErrorCode::Ok && out) liveTextureViews_.push_back(out);
        return r;
    }

    Status createCommandPool(QueueType qt, CommandPool*& out) override {
        if (destroyed_) { logError("[Validation] createCommandPool: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        CommandPool* innerPool = nullptr;
        Status r = inner_->createCommandPool(qt, innerPool);
        if (r != ErrorCode::Ok || !innerPool) { out = nullptr; return r; }
        out = new ValidatedCommandPool(innerPool);
        liveCommandPools_.push_back(out);
        return ErrorCode::Ok;
    }

    Status createFence(u64 initialValue, Fence*& out) override {
        if (destroyed_) { logError("[Validation] createFence: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        Fence* innerFence = nullptr;
        Status r = inner_->createFence(initialValue, innerFence);
        if (r != ErrorCode::Ok || !innerFence) { out = nullptr; return r; }
        out = new ValidatedFence(innerFence);
        liveFences_.push_back(out);
        return ErrorCode::Ok;
    }

    Status createSwapChain(Surface* surface, const SwapChainDesc& d, SwapChain*& out) override {
        if (destroyed_) { logError("[Validation] createSwapChain: device destroyed"); out = nullptr; return ErrorCode::Unknown; }
        SwapChain* innerSc = nullptr;
        Status r = inner_->createSwapChain(surface, d, innerSc);
        if (r != ErrorCode::Ok || !innerSc) { out = nullptr; return r; }
        out = new ValidatedSwapChain(innerSc);
        liveSwapChains_.push_back(out);
        return ErrorCode::Ok;
    }

    // ---- Mesh/RT (forwarded, validated for destroyed state) ----
    Status createMeshPipeline(const MeshPipelineDesc& d, MeshPipeline*& out) override {
        if (destroyed_) { out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->createMeshPipeline(d, out);
        if (r == ErrorCode::Ok && out) liveMeshPipelines_.push_back(out);
        return r;
    }
    void destroyMeshPipeline(MeshPipeline*& p) override { removeAndDestroy(liveMeshPipelines_, p, [&](auto*& x){ inner_->destroyMeshPipeline(x); }); }

    Status createAccelStruct(const AccelStructDesc& d, AccelStruct*& out) override {
        if (destroyed_) { out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->createAccelStruct(d, out);
        if (r == ErrorCode::Ok && out) liveAccelStructs_.push_back(out);
        return r;
    }
    void destroyAccelStruct(AccelStruct*& a) override { removeAndDestroy(liveAccelStructs_, a, [&](auto*& x){ inner_->destroyAccelStruct(x); }); }

    Status createRayTracingPipeline(const RayTracingPipelineDesc& d, RayTracingPipeline*& out) override {
        if (destroyed_) { out = nullptr; return ErrorCode::Unknown; }
        Status r = inner_->createRayTracingPipeline(d, out);
        if (r == ErrorCode::Ok && out) liveRtPipelines_.push_back(out);
        return r;
    }
    void destroyRayTracingPipeline(RayTracingPipeline*& p) override { removeAndDestroy(liveRtPipelines_, p, [&](auto*& x){ inner_->destroyRayTracingPipeline(x); }); }

    Status getShaderGroupHandles(RayTracingPipeline* p, u32 first, u32 count, Span<u8> out) override {
        return inner_->getShaderGroupHandles(p, first, count, out);
    }

    // ---- Destroy methods (with tracking removal) ----
#define V_DESTROY(Type, method, list) \
    void method(Type*& x) override { removeAndDestroy(list, x, [&](auto*& p){ inner_->method(p); }); }

    V_DESTROY(Buffer, destroyBuffer, liveBuffers_)
    V_DESTROY(Texture, destroyTexture, liveTextures_)
    V_DESTROY(TextureView, destroyTextureView, liveTextureViews_)
    V_DESTROY(Sampler, destroySampler, liveSamplers_)
    V_DESTROY(ShaderModule, destroyShaderModule, liveShaderModules_)
    V_DESTROY(BindGroupLayout, destroyBindGroupLayout, liveBindGroupLayouts_)
    V_DESTROY(BindGroup, destroyBindGroup, liveBindGroups_)
    V_DESTROY(PipelineLayout, destroyPipelineLayout, livePipelineLayouts_)
    V_DESTROY(PipelineCache, destroyPipelineCache, livePipelineCaches_)
    V_DESTROY(RenderPipeline, destroyRenderPipeline, liveRenderPipelines_)
    V_DESTROY(ComputePipeline, destroyComputePipeline, liveComputePipelines_)
    V_DESTROY(QuerySet, destroyQuerySet, liveQuerySets_)
#undef V_DESTROY

    void destroyCommandPool(CommandPool*& pool) override {
        if (!pool) return;
        removeFromList(liveCommandPools_, pool);
        auto* vp = dynamic_cast<ValidatedCommandPool*>(pool);
        if (vp) { CommandPool* innerPool = vp->inner(); inner_->destroyCommandPool(innerPool); delete vp; }
        else inner_->destroyCommandPool(pool);
        pool = nullptr;
    }

    void destroyFence(Fence*& fence) override {
        if (!fence) return;
        removeFromList(liveFences_, fence);
        auto* vf = dynamic_cast<ValidatedFence*>(fence);
        if (vf) { Fence* innerFence = vf->inner(); inner_->destroyFence(innerFence); delete vf; }
        else inner_->destroyFence(fence);
        fence = nullptr;
    }

    void destroySwapChain(SwapChain*& sc) override {
        if (!sc) return;
        removeFromList(liveSwapChains_, sc);
        auto* vs = dynamic_cast<ValidatedSwapChain*>(sc);
        if (vs) { SwapChain* innerSc = vs->inner(); inner_->destroySwapChain(innerSc); delete vs; }
        else inner_->destroySwapChain(sc);
        sc = nullptr;
    }

    void destroySurface(Surface*& s) override { inner_->destroySurface(s); }

    void waitIdle() override { inner_->waitIdle(); }

    void destroy() override {
        if (destroyed_) { logError("[Validation] Device::destroy: already destroyed"); return; }
        destroyed_ = true;
        reportLeaks();
        for (auto& w : queueWrappers_) delete w.validated;
        queueWrappers_.clear();
        inner_->destroy();
        delete this;
    }

private:
    template <typename T>
    void removeFromList(std::vector<T*>& list, T* item) {
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (*it == item) { list.erase(it); return; }
        }
    }

    template <typename T, typename Fn>
    void removeAndDestroy(std::vector<T*>& list, T*& item, Fn destroyFn) {
        if (!item) return;
        removeFromList(list, item);
        destroyFn(item);
        item = nullptr;
    }

    void reportLeaks() {
        auto report = [](const char* name, usize count) {
            if (count > 0) logWarningf("[Validation] Device destroyed with %zu live %s(s)", count, name);
        };
        report("Buffer", liveBuffers_.size());
        report("Texture", liveTextures_.size());
        report("TextureView", liveTextureViews_.size());
        report("Sampler", liveSamplers_.size());
        report("ShaderModule", liveShaderModules_.size());
        report("BindGroupLayout", liveBindGroupLayouts_.size());
        report("BindGroup", liveBindGroups_.size());
        report("PipelineLayout", livePipelineLayouts_.size());
        report("PipelineCache", livePipelineCaches_.size());
        report("RenderPipeline", liveRenderPipelines_.size());
        report("ComputePipeline", liveComputePipelines_.size());
        report("MeshPipeline", liveMeshPipelines_.size());
        report("AccelStruct", liveAccelStructs_.size());
        report("RayTracingPipeline", liveRtPipelines_.size());
        report("CommandPool", liveCommandPools_.size());
        report("Fence", liveFences_.size());
        report("SwapChain", liveSwapChains_.size());
        report("QuerySet", liveQuerySets_.size());
    }

    Device* inner_;
    bool    destroyed_ = false;

    struct QueueWrap { Queue* raw; ValidatedQueue* validated; };
    std::vector<QueueWrap> queueWrappers_;

    std::vector<Buffer*>            liveBuffers_;
    std::vector<Texture*>           liveTextures_;
    std::vector<TextureView*>       liveTextureViews_;
    std::vector<Sampler*>           liveSamplers_;
    std::vector<ShaderModule*>      liveShaderModules_;
    std::vector<BindGroupLayout*>   liveBindGroupLayouts_;
    std::vector<BindGroup*>         liveBindGroups_;
    std::vector<PipelineLayout*>    livePipelineLayouts_;
    std::vector<PipelineCache*>     livePipelineCaches_;
    std::vector<RenderPipeline*>    liveRenderPipelines_;
    std::vector<ComputePipeline*>   liveComputePipelines_;
    std::vector<MeshPipeline*>      liveMeshPipelines_;
    std::vector<AccelStruct*>       liveAccelStructs_;
    std::vector<RayTracingPipeline*>liveRtPipelines_;
    std::vector<CommandPool*>       liveCommandPools_;
    std::vector<Fence*>             liveFences_;
    std::vector<SwapChain*>         liveSwapChains_;
    std::vector<QuerySet*>          liveQuerySets_;
};

} // namespace raptor::rhi::validation
