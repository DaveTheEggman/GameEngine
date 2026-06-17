/// Null RHI backend — stub implementations for all interfaces.
/// Useful for headless testing, CI, or when no GPU is available.

module;
#include "Core/Prelude.h"  // <new> reachability for placement-new in core templates (GCC)

export module raptor.rhi.null;

import raptor.core;
import raptor.rhi;

using namespace raptor::core;

export namespace raptor::rhi::null {

// ---- Stub resource classes ----

class NullBuffer : public Buffer {
public:
    void* Map()   override { return mapped_; }
    void  Unmap() override {}
    void  allocate(u64 size) { data_.Resize(static_cast<usize>(size)); mapped_ = data_.Data(); }
private:
    Array<u8> data_;
    void* mapped_ = nullptr;
};

class NullTexture : public Texture {};
class NullTextureView : public TextureView {};
class NullSampler : public Sampler {};
class NullShaderModule : public ShaderModule {};
class NullSurface : public Surface {};
class NullCommandBuffer : public CommandBuffer {};

class NullFence : public Fence {
public:
    u64  CompletedValue() override { return value_; }
    bool Wait(u64 value, u64) override { value_ = value; return true; }
    void signal(u64 v) { value_ = v; }
private:
    u64 value_ = 0;
};

class NullQuerySet : public QuerySet {};

class NullBindGroupLayout : public BindGroupLayout {
public:
    Span<const BindGroupLayoutEntry> Entries() const override { return {}; }
};

class NullBindGroup : public BindGroup {
public:
    BindGroupLayout* Layout() override { return nullptr; }
    void UpdateBindless(Span<const BindlessUpdateEntry>) override {}
};

class NullPipelineLayout : public PipelineLayout {};

class NullPipelineCache : public PipelineCache {
public:
    u32    GetDataSize() override { return 0; }
    Status GetData(Span<u8>) override { return ErrorCode::Ok; }
};

class NullRenderPipeline : public RenderPipeline {};
class NullComputePipeline : public ComputePipeline {};
class NullMeshPipeline : public MeshPipeline {};

class NullAccelStruct : public AccelStruct {
public:
    AccelStructType Type()          const override { return AccelStructType::BottomLevel; }
    u64             DeviceAddress() const override { return 0; }
};

class NullRayTracingPipeline : public RayTracingPipeline {};

// ---- Stub encoders ----

class NullRenderPassEncoder : public RenderPassEncoder, public MeshShaderPassExt {
public:
    MeshShaderPassExt* AsMeshShaderExt() noexcept override { return this; }
    void SetPipeline(RenderPipeline*) override {}
    void SetBindGroup(u32, BindGroup*, Span<const u32>) override {}
    void SetPushConstants(ShaderStage, u32, u32, const void*) override {}
    void SetVertexBuffer(u32, Buffer*, u64) override {}
    void SetIndexBuffer(Buffer*, IndexFormat, u64) override {}
    void SetViewport(f32, f32, f32, f32, f32, f32) override {}
    void SetScissor(i32, i32, u32, u32) override {}
    void SetBlendConstant(f32, f32, f32, f32) override {}
    void SetStencilReference(u32) override {}
    void Draw(u32, u32, u32, u32) override {}
    void DrawIndexed(u32, u32, u32, i32, u32) override {}
    void DrawIndirect(Buffer*, u64, u32, u32) override {}
    void DrawIndexedIndirect(Buffer*, u64, u32, u32) override {}
    void WriteTimestamp(QuerySet*, u32) override {}
    void BeginOcclusionQuery(QuerySet*, u32) override {}
    void EndOcclusionQuery(QuerySet*, u32) override {}
    void End() override {}
    void SetMeshPipeline(MeshPipeline*) override {}
    void DrawMeshTasks(u32, u32, u32) override {}
    void DrawMeshTasksIndirect(Buffer*, u64, u32, u32) override {}
    void DrawMeshTasksIndirectCount(Buffer*, u64, Buffer*, u64, u32, u32) override {}
};

class NullComputePassEncoder : public ComputePassEncoder {
public:
    void SetPipeline(ComputePipeline*) override {}
    void SetBindGroup(u32, BindGroup*, Span<const u32>) override {}
    void SetPushConstants(ShaderStage, u32, u32, const void*) override {}
    void Dispatch(u32, u32, u32) override {}
    void DispatchIndirect(Buffer*, u64) override {}
    void ComputeBarrier() override {}
    void WriteTimestamp(QuerySet*, u32) override {}
    void End() override {}
};

class NullCommandEncoder : public CommandEncoder, public RayTracingEncoderExt {
public:
    RayTracingEncoderExt* AsRayTracingExt() noexcept override { return this; }
    NullRenderPassEncoder rpe;
    NullComputePassEncoder cpe;
    NullCommandBuffer cb;

    RenderPassEncoder*  BeginRenderPass(const RenderPassDesc&) override { return &rpe; }
    ComputePassEncoder* BeginComputePass(StringView) override { return &cpe; }
    void Barrier(const BarrierGroup&) override {}
    void CopyBufferToBuffer(Buffer*, u64, Buffer*, u64, u64) override {}
    void CopyBufferToTexture(Buffer*, Texture*, const BufferTextureCopyRegion&) override {}
    void CopyTextureToBuffer(Texture*, Buffer*, const BufferTextureCopyRegion&) override {}
    void CopyTextureToTexture(Texture*, Texture*, const TextureCopyRegion&) override {}
    void Blit(Texture*, Texture*) override {}
    void GenerateMipmaps(Texture*) override {}
    void ResolveTexture(Texture*, Texture*) override {}
    void ResetQuerySet(QuerySet*, u32, u32) override {}
    void WriteTimestamp(QuerySet*, u32) override {}
    void ResolveQuerySet(QuerySet*, u32, u32, Buffer*, u64) override {}
    void BeginDebugLabel(StringView, f32, f32, f32, f32) override {}
    void EndDebugLabel() override {}
    void InsertDebugLabel(StringView, f32, f32, f32, f32) override {}
    CommandBuffer* Finish() override { return &cb; }

    // RayTracingEncoderExt
    void BuildBottomLevelAccelStruct(AccelStruct*, Buffer*, u64, Span<const AccelStructGeometryTriangles>, Span<const AccelStructGeometryAABBs>) override {}
    void BuildTopLevelAccelStruct(AccelStruct*, Buffer*, u64, Buffer*, u64, u32) override {}
    void SetRayTracingPipeline(RayTracingPipeline*) override {}
    void SetBindGroup(u32, BindGroup*, Span<const u32>) override {}
    void SetPushConstants(ShaderStage, u32, u32, const void*) override {}
    void TraceRays(Buffer*, u64, u64, Buffer*, u64, u64, Buffer*, u64, u64, u32, u32, u32) override {}
};

class NullCommandPool : public CommandPool {
public:
    NullCommandEncoder enc;
    Status CreateEncoder(CommandEncoder*& out) override { out = &enc; return ErrorCode::Ok; }
    void   DestroyEncoder(CommandEncoder*&) override {}
    void   Reset() override {}
};

class NullTransferBatch : public TransferBatch {
public:
    void   WriteBuffer(Buffer*, u64, Span<const u8>) override {}
    void   WriteTexture(Texture*, Span<const u8>, const TextureDataLayout&, Extent3D, u32, u32) override {}
    Status Submit() override { return ErrorCode::Ok; }
    Status SubmitAsync(Fence*, u64) override { return ErrorCode::Ok; }
    void   Reset() override {}
    void   Destroy() override {}
};

class NullSwapChain : public SwapChain {
public:
    NullTexture tex;
    NullTextureView view;
    TextureFormat format_  = TextureFormat::BGRA8UnormSrgb;
    u32           width_   = 0;
    u32           height_  = 0;
    u32           count_   = 2;
    u32           imgIdx_  = 0;

    TextureFormat Format()            const override { return format_; }
    u32           Width()             const override { return width_; }
    u32           Height()            const override { return height_; }
    u32           BufferCount()       const override { return count_; }
    u32           CurrentImageIndex() const override { return imgIdx_; }
    Status        AcquireNextImage()        override { imgIdx_ = (imgIdx_ + 1) % count_; return ErrorCode::Ok; }
    Texture*      CurrentTexture()          override { return &tex; }
    TextureView*  CurrentTextureView()      override { return &view; }
    Status        Present(Queue*)           override { return ErrorCode::Ok; }
    Status        Resize(u32 w, u32 h)      override { width_ = w; height_ = h; return ErrorCode::Ok; }
};

class NullQueue : public Queue {
public:
    NullTransferBatch tb;
    void Submit(Span<CommandBuffer* const>) override {}
    void Submit(Span<CommandBuffer* const>, Fence* f, u64 v) override { if (auto* nf = static_cast<NullFence*>(f)) nf->signal(v); }
    void Submit(Span<CommandBuffer* const>, Span<Fence* const>, Span<const u64>, Fence* f, u64 v) override { if (auto* nf = static_cast<NullFence*>(f)) nf->signal(v); }
    void WaitIdle() override {}
    Status CreateTransferBatch(TransferBatch*& out) override { out = &tb; return ErrorCode::Ok; }
    void DestroyTransferBatch(TransferBatch*&) override {}
    f32 TimestampPeriod() const override { return 1.0f; }
};

// ---- Null Device ----

class NullDevice : public Device {
public:
    NullQueue gfxQueue, compQueue, xferQueue;

    NullDevice() {
        type = DeviceType::Null;
        gfxQueue.queueType  = QueueType::Graphics;
        compQueue.queueType = QueueType::Compute;
        xferQueue.queueType = QueueType::Transfer;
    }

    Queue* GetQueue(QueueType t, u32) override {
        switch (t) {
        case QueueType::Graphics: return &gfxQueue;
        case QueueType::Compute:  return &compQueue;
        case QueueType::Transfer: return &xferQueue;
        } return nullptr;
    }
    u32 GetQueueCount(QueueType) override { return 1; }
    FormatSupport GetFormatSupport(TextureFormat) override { return FormatSupport::Texture | FormatSupport::ColorAttachment | FormatSupport::DepthStencil; }

    Status CreateBuffer(const BufferDesc& d, Buffer*& out) override {
        auto* b = new NullBuffer(); b->desc = d; b->allocate(d.size); out = b; return ErrorCode::Ok;
    }
    Status CreateTexture(const TextureDesc& d, Texture*& out) override { auto* t = new NullTexture(); t->desc = d; out = t; return ErrorCode::Ok; }
    Status CreateTextureView(Texture* tex, const TextureViewDesc& d, TextureView*& out) override { auto* v = new NullTextureView(); v->desc = d; v->texture = tex; out = v; return ErrorCode::Ok; }
    Status CreateSampler(const SamplerDesc& d, Sampler*& out) override { auto* s = new NullSampler(); s->desc = d; out = s; return ErrorCode::Ok; }
    Status CreateShaderModule(const ShaderModuleDesc&, ShaderModule*& out) override { out = new NullShaderModule(); return ErrorCode::Ok; }
    Status CreateBindGroupLayout(const BindGroupLayoutDesc&, BindGroupLayout*& out) override { out = new NullBindGroupLayout(); return ErrorCode::Ok; }
    Status CreateBindGroup(const BindGroupDesc&, BindGroup*& out) override { out = new NullBindGroup(); return ErrorCode::Ok; }
    Status CreatePipelineLayout(const PipelineLayoutDesc&, PipelineLayout*& out) override { out = new NullPipelineLayout(); return ErrorCode::Ok; }
    Status CreatePipelineCache(const PipelineCacheDesc&, PipelineCache*& out) override { out = new NullPipelineCache(); return ErrorCode::Ok; }
    Status CreateRenderPipeline(const RenderPipelineDesc&, RenderPipeline*& out) override { out = new NullRenderPipeline(); return ErrorCode::Ok; }
    Status CreateComputePipeline(const ComputePipelineDesc&, ComputePipeline*& out) override { out = new NullComputePipeline(); return ErrorCode::Ok; }
    Status CreateCommandPool(QueueType, CommandPool*& out) override { out = new NullCommandPool(); return ErrorCode::Ok; }
    Status CreateFence(u64, Fence*& out) override { out = new NullFence(); return ErrorCode::Ok; }
    Status CreateQuerySet(const QuerySetDesc& d, QuerySet*& out) override { auto* q = new NullQuerySet(); q->type = d.type; q->count = d.count; out = q; return ErrorCode::Ok; }
    Status CreateSwapChain(Surface*, const SwapChainDesc& d, SwapChain*& out) override {
        auto* sc = new NullSwapChain(); sc->format_ = d.format; sc->width_ = d.width; sc->height_ = d.height; sc->count_ = d.bufferCount; out = sc; return ErrorCode::Ok;
    }

    void DestroyBuffer(Buffer*& x)            override { delete x; x = nullptr; }
    void DestroyTexture(Texture*& x)          override { delete x; x = nullptr; }
    void DestroyTextureView(TextureView*& x)  override { delete x; x = nullptr; }
    void DestroySampler(Sampler*& x)          override { delete x; x = nullptr; }
    void DestroyShaderModule(ShaderModule*& x)override { delete x; x = nullptr; }
    void DestroyBindGroupLayout(BindGroupLayout*& x) override { delete x; x = nullptr; }
    void DestroyBindGroup(BindGroup*& x)      override { delete x; x = nullptr; }
    void DestroyPipelineLayout(PipelineLayout*& x) override { delete x; x = nullptr; }
    void DestroyPipelineCache(PipelineCache*& x) override { delete x; x = nullptr; }
    void DestroyRenderPipeline(RenderPipeline*& x) override { delete x; x = nullptr; }
    void DestroyComputePipeline(ComputePipeline*& x) override { delete x; x = nullptr; }
    void DestroyCommandPool(CommandPool*& x)  override { delete x; x = nullptr; }
    void DestroyFence(Fence*& x)              override { delete x; x = nullptr; }
    void DestroyQuerySet(QuerySet*& x)        override { delete x; x = nullptr; }
    void DestroySwapChain(SwapChain*& x)      override { delete x; x = nullptr; }
    void DestroySurface(Surface*& x)          override { delete x; x = nullptr; }

    void WaitIdle() override {}
    void Destroy() override { delete this; }
};

// ---- Null Adapter ----

class NullAdapter : public Adapter {
public:
    void GetInfo(AdapterInfo& out) override {
        out.name     = u"Null Device";
        out.vendorId = 0;
        out.deviceId = 0;
        out.type     = AdapterType::Cpu;
    }
    Status CreateDevice(const DeviceDesc&, Device*& out) override {
        out = new NullDevice();
        return ErrorCode::Ok;
    }
};

// ---- Null Backend ----

class NullBackend : public Backend {
public:
    NullAdapter adapter;
    Adapter*    adapterPtr = &adapter;

    Span<Adapter* const> EnumerateAdapters() override {
        return Span<Adapter* const>(&adapterPtr, 1);
    }

    Status CreateSurface(void*, void*, Surface*& out) override {
        out = new NullSurface();
        return ErrorCode::Ok;
    }

    void Destroy() override { delete this; }
};

/// Creates a null backend for headless / GPU-less testing.
Status CreateNullBackend(Backend*& out) {
    auto* b = new NullBackend();
    b->isInitialized = true;
    out = b;
    return ErrorCode::Ok;
}

} // namespace raptor::rhi::null
