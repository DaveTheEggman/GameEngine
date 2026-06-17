/// DX12 implementation of Device.
/// Creates ID3D12Device, manages descriptor heaps, queues, command signatures,
/// and an internal blit pipeline for texture copy / mipmap generation.
/// Ported from Sedulous.RHI.DX12/DX12Device.bf.

module;

#include "DxIncludes.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

export module raptor.rhi.dx12:device;

import raptor.core;
import raptor.rhi;
import :conversions;
import :adapter;
import :surface;
import :descriptor_heap;
import :gpu_descriptor_heap;
import :buffer;
import :texture;
import :texture_view;
import :sampler;
import :shader_module;
import :fence;
import :query_set;
import :bind_group_layout;
import :bind_group;
import :pipeline_layout;
import :pipeline_cache;
import :render_pipeline;
import :compute_pipeline;
import :mesh_pipeline;
import :accel_struct;
import :ray_tracing_pipeline;
import :command_pool;
import :command_encoder;
import :render_pass_encoder;
import :compute_pass_encoder;
import :queue;
import :swap_chain;

export namespace raptor::rhi::dx12 {

class DxDeviceImpl : public Device {
public:
    Status init(DxAdapterImpl* adapter, const DeviceDesc& desc) {
        adapter_ = adapter;

        // Create device at feature level 12.0.
        HRESULT hr = D3D12CreateDevice(
            adapter->handle(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_));
        if (FAILED(hr)) {
            logErrorf("DxDevice: D3D12CreateDevice failed (0x%08X)", static_cast<unsigned>(hr));
            return ErrorCode::Unknown;
        }

        // Suppress noisy debug layer warnings.
        {
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
                D3D12_MESSAGE_ID suppressIds[] = {
                    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                    D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                };
                D3D12_INFO_QUEUE_FILTER filter{};
                filter.DenyList.NumIDs  = static_cast<UINT>(std::size(suppressIds));
                filter.DenyList.pIDList = suppressIds;
                infoQueue->AddStorageFilterEntries(&filter);
                infoQueue_ = infoQueue;
            }
        }

        // --- Descriptor heap allocators (CPU-side, for staging) ---
        rtvHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256);
        dsvHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64);
        srvHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096);
        samplerHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256);

        // --- GPU-visible descriptor heaps (shader-visible) ---
        gpuSrvHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, true);
        gpuSamplerHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);

        // --- CPU-visible descriptor heaps (non-shader-visible, bind groups write here) ---
        cpuSrvHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, false);
        cpuSamplerHeap_.init(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, false);

        // --- Create queues ---
        u32 graphicsCount = std::max(desc.graphicsQueueCount, 1u);
        for (u32 i = 0; i < graphicsCount; ++i) {
            auto* q = new DxQueueImpl();
            if (q->init(device_.Get(), QueueType::Graphics, this) != ErrorCode::Ok) {
                delete q; break;
            }
            graphicsQueues_.push_back(q);
        }
        for (u32 i = 0; i < desc.computeQueueCount; ++i) {
            auto* q = new DxQueueImpl();
            if (q->init(device_.Get(), QueueType::Compute, this) != ErrorCode::Ok) {
                delete q; break;
            }
            computeQueues_.push_back(q);
        }
        for (u32 i = 0; i < desc.transferQueueCount; ++i) {
            auto* q = new DxQueueImpl();
            if (q->init(device_.Get(), QueueType::Transfer, this) != ErrorCode::Ok) {
                delete q; break;
            }
            transferQueues_.push_back(q);
        }

        // --- Cached command signatures for indirect execution ---
        createIndirectCommandSignatures();

        // --- Internal blit pipeline ---
        createBlitPipeline();

        // --- Detect mesh shader & ray tracing support ---
        detectExtensionSupport();

        // --- Populate features ---
        type     = DeviceType::DX12;
        features = adapter->buildFeatures();

        // --- RT handle properties (DX12 constants) ---
        if (rtEnabled_) {
            shaderGroupHandleSize      = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32
            shaderGroupHandleAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT; // 32
            shaderGroupBaseAlignment   = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;  // 64
        }

        return ErrorCode::Ok;
    }

    // ==================================================================
    // Device interface -- Queues
    // ==================================================================

    Queue* getQueue(QueueType t, u32 index) override {
        switch (t) {
        case QueueType::Graphics: return index < graphicsQueues_.size()  ? graphicsQueues_[index]  : nullptr;
        case QueueType::Compute:  return index < computeQueues_.size()   ? computeQueues_[index]   : nullptr;
        case QueueType::Transfer: return index < transferQueues_.size()  ? transferQueues_[index]   : nullptr;
        }
        return nullptr;
    }

    u32 getQueueCount(QueueType t) override {
        switch (t) {
        case QueueType::Graphics: return static_cast<u32>(graphicsQueues_.size());
        case QueueType::Compute:  return static_cast<u32>(computeQueues_.size());
        case QueueType::Transfer: return static_cast<u32>(transferQueues_.size());
        }
        return 0;
    }

    FormatSupport getFormatSupport(TextureFormat format) override {
        // DX12 supports D24_S8 on all hardware and most formats broadly.
        // A full implementation would call CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT).
        return FormatSupport::Texture | FormatSupport::ColorAttachment |
               FormatSupport::DepthStencil | FormatSupport::Buffer |
               FormatSupport::VertexBuffer | FormatSupport::BlendableColor |
               FormatSupport::LinearFilter;
    }

    // ==================================================================
    // Device interface -- Resource creation
    // ==================================================================

    Status createBuffer(const BufferDesc& d, Buffer*& out) override {
        auto* b = new DxBufferImpl();
        if (b->init(device_.Get(), d) != ErrorCode::Ok) { delete b; out = nullptr; return ErrorCode::Unknown; }
        setDebugName(b->handle(), d.label);
        out = b;
        return ErrorCode::Ok;
    }

    Status createTexture(const TextureDesc& d, Texture*& out) override {
        auto* t = new DxTextureImpl();
        if (t->init(device_.Get(), d) != ErrorCode::Ok) { delete t; out = nullptr; return ErrorCode::Unknown; }
        setDebugName(t->handle(), d.label);
        out = t;
        return ErrorCode::Ok;
    }

    Status createTextureView(Texture* tex, const TextureViewDesc& d, TextureView*& out) override {
        auto* dxTex = static_cast<DxTextureImpl*>(tex);
        if (!dxTex) {
            logError("DxDevice: cast to DxTextureImpl failed");
            out = nullptr;
            return ErrorCode::Unknown;
        }
        auto* v = new DxTextureViewImpl();
        if (v->init(device_.Get(), dxTex, d, &srvHeap_, &rtvHeap_, &dsvHeap_) != ErrorCode::Ok) {
            delete v; out = nullptr; return ErrorCode::Unknown;
        }
        out = v;
        return ErrorCode::Ok;
    }

    Status createSampler(const SamplerDesc& d, Sampler*& out) override {
        auto* s = new DxSamplerImpl();
        if (s->init(device_.Get(), d, &samplerHeap_) != ErrorCode::Ok) {
            delete s; out = nullptr; return ErrorCode::Unknown;
        }
        out = s;
        return ErrorCode::Ok;
    }

    Status createShaderModule(const ShaderModuleDesc& d, ShaderModule*& out) override {
        auto* m = new DxShaderModuleImpl();
        if (m->init(d) != ErrorCode::Ok) { delete m; out = nullptr; return ErrorCode::Unknown; }
        out = m;
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Binding & Pipelines
    // ==================================================================

    Status createBindGroupLayout(const BindGroupLayoutDesc& d, BindGroupLayout*& out) override {
        auto* l = new DxBindGroupLayoutImpl();
        if (l->init(d) != ErrorCode::Ok) { delete l; out = nullptr; return ErrorCode::Unknown; }
        out = l;
        return ErrorCode::Ok;
    }

    Status createBindGroup(const BindGroupDesc& d, BindGroup*& out) override {
        auto* g = new DxBindGroupImpl();
        if (g->init(device_.Get(), d, &cpuSrvHeap_, &cpuSamplerHeap_) != ErrorCode::Ok) {
            delete g; out = nullptr; return ErrorCode::Unknown;
        }
        out = g;
        return ErrorCode::Ok;
    }

    Status createPipelineLayout(const PipelineLayoutDesc& d, PipelineLayout*& out) override {
        auto* l = new DxPipelineLayoutImpl();
        if (l->init(device_.Get(), d) != ErrorCode::Ok) {
            logError("DxDevice: createPipelineLayout failed");
            delete l; out = nullptr; return ErrorCode::Unknown;
        }
        setDebugName(l->handle(), d.label);
        out = l;
        return ErrorCode::Ok;
    }

    Status createPipelineCache(const PipelineCacheDesc& d, PipelineCache*& out) override {
        auto* c = new DxPipelineCacheImpl();
        if (c->init(device_.Get(), d) != ErrorCode::Ok) { delete c; out = nullptr; return ErrorCode::Unknown; }
        if (c->handle()) setDebugName(c->handle(), d.label);
        out = c;
        return ErrorCode::Ok;
    }

    Status createRenderPipeline(const RenderPipelineDesc& d, RenderPipeline*& out) override {
        auto* p = new DxRenderPipelineImpl();
        if (p->init(device_.Get(), d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        setDebugName(p->handle(), d.label);
        out = p;
        return ErrorCode::Ok;
    }

    Status createComputePipeline(const ComputePipelineDesc& d, ComputePipeline*& out) override {
        auto* p = new DxComputePipelineImpl();
        if (p->init(device_.Get(), d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        setDebugName(p->handle(), d.label);
        out = p;
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Mesh shader (folded in)
    // ==================================================================

    Status createMeshPipeline(const MeshPipelineDesc& d, MeshPipeline*& out) override {
        if (!meshEnabled_) { out = nullptr; return ErrorCode::NotSupported; }
        auto* p = new DxMeshPipelineImpl();
        if (p->init(device_.Get(), d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        setDebugName(p->handle(), d.label);
        out = p;
        return ErrorCode::Ok;
    }

    void destroyMeshPipeline(MeshPipeline*& p) override {
        if (p) { static_cast<DxMeshPipelineImpl*>(p)->cleanup(); delete p; p = nullptr; }
    }

    // ==================================================================
    // Ray tracing (folded in)
    // ==================================================================

    Status createAccelStruct(const AccelStructDesc& d, AccelStruct*& out) override {
        if (!rtEnabled_) { out = nullptr; return ErrorCode::NotSupported; }
        auto* a = new DxAccelStructImpl();
        if (a->init(device_.Get(), d) != ErrorCode::Ok) { delete a; out = nullptr; return ErrorCode::Unknown; }
        out = a;
        return ErrorCode::Ok;
    }

    void destroyAccelStruct(AccelStruct*& a) override {
        if (a) { static_cast<DxAccelStructImpl*>(a)->cleanup(); delete a; a = nullptr; }
    }

    Status createRayTracingPipeline(const RayTracingPipelineDesc& d, RayTracingPipeline*& out) override {
        if (!rtEnabled_) { out = nullptr; return ErrorCode::NotSupported; }
        auto* p = new DxRayTracingPipelineImpl();
        if (p->init(device_.Get(), d) != ErrorCode::Ok) { delete p; out = nullptr; return ErrorCode::Unknown; }
        out = p;
        return ErrorCode::Ok;
    }

    void destroyRayTracingPipeline(RayTracingPipeline*& p) override {
        if (p) { static_cast<DxRayTracingPipelineImpl*>(p)->cleanup(); delete p; p = nullptr; }
    }

    Status getShaderGroupHandles(RayTracingPipeline* pipeline, u32 firstGroup,
                                  u32 groupCount, Span<u8> outData) override {
        if (!rtEnabled_) return ErrorCode::NotSupported;
        auto* dxPipeline = static_cast<DxRayTracingPipelineImpl*>(pipeline);
        if (!dxPipeline || !dxPipeline->properties()) {
            logError("DxDevice: pipeline or properties is null");
            return ErrorCode::Unknown;
        }

        constexpr u32 handleSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32
        if (outData.count() < static_cast<usize>(groupCount * handleSize)) {
            logError("DxDevice: output buffer too small for shader group handles");
            return ErrorCode::Unknown;
        }

        auto exportNames = dxPipeline->groupExportNames();
        for (u32 i = 0; i < groupCount; ++i) {
            u32 groupIdx = firstGroup + i;
            if (groupIdx >= exportNames.count()) {
                logError("DxDevice: shader group index out of range");
                return ErrorCode::Unknown;
            }
            const auto& exportName = exportNames[groupIdx];
            // Convert narrow name to wide for DX12 API.
            std::wstring wide(exportName.begin(), exportName.end());
            void* identifier = dxPipeline->properties()->GetShaderIdentifier(wide.c_str());
            if (!identifier) {
                logError("DxDevice: GetShaderIdentifier returned null");
                return ErrorCode::Unknown;
            }
            std::memcpy(outData.data() + (i * handleSize), identifier, handleSize);
        }
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Commands
    // ==================================================================

    Status createCommandPool(QueueType qt, CommandPool*& out) override {
        auto* p = new DxCommandPoolImpl();
        if (p->init(this, device_.Get(), qt,
                    &cpuSrvHeap_, &gpuSrvHeap_,
                    &cpuSamplerHeap_, &gpuSamplerHeap_) != ErrorCode::Ok) {
            delete p; out = nullptr; return ErrorCode::Unknown;
        }
        out = p;
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Synchronization
    // ==================================================================

    Status createFence(u64 initialValue, Fence*& out) override {
        auto* f = new DxFenceImpl();
        if (f->init(device_.Get(), initialValue) != ErrorCode::Ok) { delete f; out = nullptr; return ErrorCode::Unknown; }
        out = f;
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Queries
    // ==================================================================

    Status createQuerySet(const QuerySetDesc& d, QuerySet*& out) override {
        auto* q = new DxQuerySetImpl();
        if (q->init(device_.Get(), d) != ErrorCode::Ok) { delete q; out = nullptr; return ErrorCode::Unknown; }
        setDebugName(q->handle(), d.label);
        out = q;
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Presentation
    // ==================================================================

    Status createSwapChain(Surface* surface, const SwapChainDesc& d, SwapChain*& out) override {
        auto* dxSurface = static_cast<DxSurfaceImpl*>(surface);
        if (!dxSurface) {
            logError("DxDevice: cast to DxSurfaceImpl failed");
            out = nullptr;
            return ErrorCode::Unknown;
        }

        // Need a graphics queue for swap chain.
        if (graphicsQueues_.empty()) { out = nullptr; return ErrorCode::Unknown; }

        auto* sc = new DxSwapChainImpl();
        if (sc->init(device_.Get(), adapter_->factory(),
                     graphicsQueues_[0]->handle(),
                     dxSurface, d,
                     &srvHeap_, &rtvHeap_, &dsvHeap_) != ErrorCode::Ok) {
            delete sc; out = nullptr; return ErrorCode::Unknown;
        }
        out = sc;
        return ErrorCode::Ok;
    }

    // ==================================================================
    // Resource destruction
    // ==================================================================

    void destroyBuffer(Buffer*& b)              override { if (b) { static_cast<DxBufferImpl*>(b)->cleanup(); delete b; b = nullptr; } }
    void destroyTexture(Texture*& t)            override { if (t) { static_cast<DxTextureImpl*>(t)->cleanup(); delete t; t = nullptr; } }
    void destroyTextureView(TextureView*& v)    override { if (v) { static_cast<DxTextureViewImpl*>(v)->cleanup(); delete v; v = nullptr; } }
    void destroySampler(Sampler*& s)            override { if (s) { static_cast<DxSamplerImpl*>(s)->cleanup(); delete s; s = nullptr; } }
    void destroyShaderModule(ShaderModule*& m)  override { if (m) { static_cast<DxShaderModuleImpl*>(m)->cleanup(); delete m; m = nullptr; } }
    void destroyBindGroupLayout(BindGroupLayout*& l) override { if (l) { delete l; l = nullptr; } }
    void destroyBindGroup(BindGroup*& g)        override { if (g) { static_cast<DxBindGroupImpl*>(g)->cleanup(); delete g; g = nullptr; } }
    void destroyPipelineLayout(PipelineLayout*& l) override { if (l) { static_cast<DxPipelineLayoutImpl*>(l)->cleanup(); delete l; l = nullptr; } }
    void destroyPipelineCache(PipelineCache*& c) override { if (c) { static_cast<DxPipelineCacheImpl*>(c)->cleanup(); delete c; c = nullptr; } }
    void destroyRenderPipeline(RenderPipeline*& p) override { if (p) { static_cast<DxRenderPipelineImpl*>(p)->cleanup(); delete p; p = nullptr; } }
    void destroyComputePipeline(ComputePipeline*& p) override { if (p) { static_cast<DxComputePipelineImpl*>(p)->cleanup(); delete p; p = nullptr; } }
    void destroyCommandPool(CommandPool*& p)    override { if (p) { static_cast<DxCommandPoolImpl*>(p)->cleanup(); delete p; p = nullptr; } }
    void destroyFence(Fence*& f)                override { if (f) { static_cast<DxFenceImpl*>(f)->cleanup(); delete f; f = nullptr; } }
    void destroyQuerySet(QuerySet*& q)          override { if (q) { static_cast<DxQuerySetImpl*>(q)->cleanup(); delete q; q = nullptr; } }
    void destroySwapChain(SwapChain*& sc)       override { if (sc) { static_cast<DxSwapChainImpl*>(sc)->cleanup(); delete sc; sc = nullptr; } }
    void destroySurface(Surface*& s)            override { if (s) { delete s; s = nullptr; } }

    // ==================================================================
    // Lifecycle
    // ==================================================================

    void waitIdle() override {
        for (auto* q : graphicsQueues_) q->waitIdle();
        for (auto* q : computeQueues_)  q->waitIdle();
        for (auto* q : transferQueues_) q->waitIdle();
        drainDebugMessages();
    }

    void drainDebugMessages() {
        if (!infoQueue_) return;
        UINT64 count = infoQueue_->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T len = 0;
            infoQueue_->GetMessage(i, nullptr, &len);
            if (len == 0) continue;
            auto* msg = static_cast<D3D12_MESSAGE*>(std::malloc(len));
            if (infoQueue_->GetMessage(i, msg, &len) == S_OK) {
                if (msg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                    std::fprintf(stderr, "[DX12 %s] %.*s\n",
                        msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ? "ERROR" :
                        msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING ? "WARN" : "CORRUPT",
                        static_cast<int>(msg->DescriptionByteLength), msg->pDescription);
            }
            std::free(msg);
        }
        infoQueue_->ClearStoredMessages();
    }

    void destroy() override {
        waitIdle();

        // Queues.
        for (auto* q : graphicsQueues_) { q->cleanup(); delete q; }
        for (auto* q : computeQueues_)  { q->cleanup(); delete q; }
        for (auto* q : transferQueues_) { q->cleanup(); delete q; }
        graphicsQueues_.clear();
        computeQueues_.clear();
        transferQueues_.clear();

        // Blit pipeline.
        for (auto& [fmt, pso] : blitPsoCache_)
            pso.Reset();
        blitPsoCache_.clear();
        blitVsBlob_.Reset();
        blitPsBlob_.Reset();
        blitRootSignature_.Reset();

        // Command signatures.
        drawSignature_.Reset();
        drawIndexedSignature_.Reset();
        dispatchSignature_.Reset();
        dispatchMeshSignature_.Reset();

        // Descriptor heaps.
        cpuSrvHeap_.destroy();
        cpuSamplerHeap_.destroy();
        gpuSrvHeap_.destroy();
        gpuSamplerHeap_.destroy();
        rtvHeap_.destroy();
        dsvHeap_.destroy();
        srvHeap_.destroy();
        samplerHeap_.destroy();

        // Report live objects in debug builds.
#ifdef _DEBUG
        {
            ComPtr<ID3D12DebugDevice> debugDevice;
            if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&debugDevice)))) {
                debugDevice->ReportLiveDeviceObjects(
                    static_cast<D3D12_RLDO_FLAGS>(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL));
            }
        }
#endif

        device_.Reset();
        delete this;
    }

    // ==================================================================
    // Internal accessors (encoders, swap chain, etc. need these)
    // ==================================================================

    [[nodiscard]] ID3D12Device* handle() const { return device_.Get(); }
    [[nodiscard]] DxAdapterImpl* adapter() const { return adapter_; }

    [[nodiscard]] DxDescriptorHeapAllocator* rtvHeap()     { return &rtvHeap_; }
    [[nodiscard]] DxDescriptorHeapAllocator* dsvHeap()     { return &dsvHeap_; }
    [[nodiscard]] DxDescriptorHeapAllocator* srvHeap()     { return &srvHeap_; }
    [[nodiscard]] DxDescriptorHeapAllocator* samplerHeap() { return &samplerHeap_; }

    [[nodiscard]] DxGpuDescriptorHeap* gpuSrvHeap()     { return &gpuSrvHeap_; }
    [[nodiscard]] DxGpuDescriptorHeap* gpuSamplerHeap() { return &gpuSamplerHeap_; }
    [[nodiscard]] DxGpuDescriptorHeap* cpuSrvHeap()     { return &cpuSrvHeap_; }
    [[nodiscard]] DxGpuDescriptorHeap* cpuSamplerHeap() { return &cpuSamplerHeap_; }

    [[nodiscard]] ID3D12CommandSignature* drawSignature()        const { return drawSignature_.Get(); }
    [[nodiscard]] ID3D12CommandSignature* drawIndexedSignature()  const { return drawIndexedSignature_.Get(); }
    [[nodiscard]] ID3D12CommandSignature* dispatchSignature()     const { return dispatchSignature_.Get(); }
    [[nodiscard]] ID3D12CommandSignature* dispatchMeshSignature() const { return dispatchMeshSignature_.Get(); }
    [[nodiscard]] ID3D12RootSignature*    blitRootSignature()    const { return blitRootSignature_.Get(); }

    [[nodiscard]] bool meshEnabled() const { return meshEnabled_; }
    [[nodiscard]] bool rtEnabled()   const { return rtEnabled_; }

    /// Gets or creates a blit PSO for the given render target format.
    ID3D12PipelineState* getOrCreateBlitPSO(DXGI_FORMAT format) {
        if (!blitRootSignature_) return nullptr;

        std::lock_guard lock(blitMutex_);

        auto it = blitPsoCache_.find(format);
        if (it != blitPsoCache_.end()) return it->second.Get();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psd{};
        psd.pRootSignature = blitRootSignature_.Get();
        psd.VS = blitVsBytecode_;
        psd.PS = blitPsBytecode_;
        psd.InputLayout = { nullptr, 0 };
        psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psd.RasterizerState.FillMode         = D3D12_FILL_MODE_SOLID;
        psd.RasterizerState.CullMode         = D3D12_CULL_MODE_NONE;
        psd.RasterizerState.DepthClipEnable  = FALSE;
        psd.BlendState.RenderTarget[0].BlendEnable          = FALSE;
        psd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0x0F;
        psd.DepthStencilState.DepthEnable   = FALSE;
        psd.DepthStencilState.StencilEnable = FALSE;
        psd.DSVFormat        = DXGI_FORMAT_UNKNOWN;
        psd.NumRenderTargets = 1;
        psd.RTVFormats[0]    = format;
        psd.SampleDesc.Count = 1;
        psd.SampleMask       = UINT_MAX;

        ComPtr<ID3D12PipelineState> newPso;
        if (SUCCEEDED(device_->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&newPso)))) {
            auto* raw = newPso.Get();
            blitPsoCache_[format] = std::move(newPso);
            return raw;
        }
        return nullptr;
    }

    /// Sets a debug name on a DX12 object (visible in PIX, VS Graphics Debugger, etc.).
    /// Works with any type that inherits from ID3D12Object (Resource, PSO, QueryHeap, etc.).
    template<typename T>
    static void setDebugName(T* obj, StringView name) {
        if (!obj || name.isEmpty()) return;
        // Convert narrow to wide.
        std::wstring wide;
        wide.reserve(name.length());
        for (usize i = 0; i < name.length(); ++i)
            wide.push_back(static_cast<wchar_t>(name[i]));
        obj->SetName(wide.c_str());
    }

private:
    // ------------------------------------------------------------------
    // Indirect command signatures
    // ------------------------------------------------------------------

    void createIndirectCommandSignatures() {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc{};
        D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs   = &argDesc;
        sigDesc.NodeMask         = 0;

        // Draw.
        argDesc.Type        = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        sigDesc.ByteStride  = 16; // sizeof(D3D12_DRAW_ARGUMENTS): 4 x uint32
        device_->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&drawSignature_));

        // DrawIndexed.
        argDesc.Type        = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        sigDesc.ByteStride  = 20; // sizeof(D3D12_DRAW_INDEXED_ARGUMENTS): 5 x uint32
        device_->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&drawIndexedSignature_));

        // Dispatch.
        argDesc.Type        = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        sigDesc.ByteStride  = 12; // sizeof(D3D12_DISPATCH_ARGUMENTS): 3 x uint32
        device_->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&dispatchSignature_));
    }

    // ------------------------------------------------------------------
    // Extension support detection (mesh shader, ray tracing)
    // ------------------------------------------------------------------

    void detectExtensionSupport() {
        // Mesh shaders -- requires D3D12_OPTIONS7.
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        HRESULT hr = device_->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
        if (SUCCEEDED(hr) && options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
            meshEnabled_ = true;

            // DispatchMesh command signature.
            D3D12_INDIRECT_ARGUMENT_DESC argDesc{};
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
            D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
            sigDesc.ByteStride       = 12; // sizeof(D3D12_DISPATCH_MESH_ARGUMENTS): 3 x uint32
            sigDesc.NumArgumentDescs = 1;
            sigDesc.pArgumentDescs   = &argDesc;
            sigDesc.NodeMask         = 0;
            device_->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&dispatchMeshSignature_));
        }

        // Ray tracing -- requires D3D12_OPTIONS5.
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        hr = device_->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
        if (SUCCEEDED(hr) && options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            rtEnabled_ = true;
        }
    }

    // ------------------------------------------------------------------
    // Internal blit pipeline (fullscreen triangle VS + texture sample PS)
    // ------------------------------------------------------------------

    void createBlitPipeline() {
        // TODO: Blit pipeline requires D3DCompile from d3dcompiler.lib.
        // Add d3dcompiler to target_link_libraries and uncomment the code below
        // once d3dcompiler linkage is available in this project.
        //
        // The blit pipeline is used for Blit and GenerateMipmaps operations.
        const char vsSource[] = R"(
            struct VSOutput {
                float4 Position : SV_Position;
                float2 UV : TEXCOORD0;
            };
            VSOutput main(uint vertexId : SV_VertexID) {
                VSOutput output;
                output.UV = float2((vertexId << 1) & 2, vertexId & 2);
                output.Position = float4(output.UV * float2(2, -2) + float2(-1, 1), 0, 1);
                return output;
            }
        )";

        const char psSource[] = R"(
            Texture2D srcTexture : register(t0);
            SamplerState srcSampler : register(s0);
            float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
                return srcTexture.Sample(srcSampler, uv);
            }
        )";

        ComPtr<ID3DBlob> errorBlob;

        // Compile VS.
        HRESULT hr = D3DCompile(vsSource, sizeof(vsSource) - 1, nullptr, nullptr, nullptr,
                                "main", "vs_5_0", 0, 0, &blitVsBlob_, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) logErrorf("DxDevice: blit VS compile error: %s",
                                      static_cast<const char*>(errorBlob->GetBufferPointer()));
            return;
        }
        errorBlob.Reset();

        // Compile PS.
        hr = D3DCompile(psSource, sizeof(psSource) - 1, nullptr, nullptr, nullptr,
                        "main", "ps_5_0", 0, 0, &blitPsBlob_, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) logErrorf("DxDevice: blit PS compile error: %s",
                                      static_cast<const char*>(errorBlob->GetBufferPointer()));
            blitVsBlob_.Reset();
            return;
        }
        errorBlob.Reset();

        blitVsBytecode_ = { blitVsBlob_->GetBufferPointer(), blitVsBlob_->GetBufferSize() };
        blitPsBytecode_ = { blitPsBlob_->GetBufferPointer(), blitPsBlob_->GetBufferSize() };

        // Root signature: 1 SRV descriptor table (t0) + 1 static linear sampler (s0).
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 1;
        srvRange.BaseShaderRegister                = 0;
        srvRange.RegisterSpace                     = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER rootParam{};
        rootParam.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParam.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParam.DescriptorTable.NumDescriptorRanges = 1;
        rootParam.DescriptorTable.pDescriptorRanges   = &srvRange;

        D3D12_STATIC_SAMPLER_DESC staticSampler{};
        staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.MaxAnisotropy    = 1;
        staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        staticSampler.MinLOD           = 0;
        staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters     = 1;
        rsDesc.pParameters       = &rootParam;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &staticSampler;

        ComPtr<ID3DBlob> signatureBlob;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                          &signatureBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) logErrorf("DxDevice: blit root sig serialize error: %s",
                                      static_cast<const char*>(errorBlob->GetBufferPointer()));
            return;
        }

        device_->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                     signatureBlob->GetBufferSize(),
                                     IID_PPV_ARGS(&blitRootSignature_));
    }

    // ------------------------------------------------------------------
    // Member data
    // ------------------------------------------------------------------

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    DxAdapterImpl*       adapter_ = nullptr;

    // Queues.
    std::vector<DxQueueImpl*> graphicsQueues_;
    std::vector<DxQueueImpl*> computeQueues_;
    std::vector<DxQueueImpl*> transferQueues_;

    // Descriptor heap allocators (CPU-side for staging).
    DxDescriptorHeapAllocator rtvHeap_;
    DxDescriptorHeapAllocator dsvHeap_;
    DxDescriptorHeapAllocator srvHeap_;
    DxDescriptorHeapAllocator samplerHeap_;

    // GPU-visible descriptor heaps (shader-visible, for command buffer binding).
    DxGpuDescriptorHeap gpuSrvHeap_;
    DxGpuDescriptorHeap gpuSamplerHeap_;

    // CPU-visible descriptor heaps (non-shader-visible, bind groups write here).
    DxGpuDescriptorHeap cpuSrvHeap_;
    DxGpuDescriptorHeap cpuSamplerHeap_;

    // Cached command signatures for indirect execution.
    ComPtr<ID3D12CommandSignature> drawSignature_;
    ComPtr<ID3D12CommandSignature> drawIndexedSignature_;
    ComPtr<ID3D12CommandSignature> dispatchSignature_;
    ComPtr<ID3D12CommandSignature> dispatchMeshSignature_;

    // Internal blit pipeline.
    ComPtr<ID3D12RootSignature>    blitRootSignature_;
    D3D12_SHADER_BYTECODE          blitVsBytecode_{};
    D3D12_SHADER_BYTECODE          blitPsBytecode_{};
    ComPtr<ID3DBlob>               blitVsBlob_;
    ComPtr<ID3DBlob>               blitPsBlob_;
    std::unordered_map<DXGI_FORMAT, ComPtr<ID3D12PipelineState>> blitPsoCache_;
    std::mutex                     blitMutex_;

    // Extension flags.
    bool meshEnabled_ = false;
    bool rtEnabled_   = false;
};

// ==================================================================
// Adapter::createDevice implementation
// ==================================================================

Status DxAdapterImpl::createDevice(const DeviceDesc& desc, Device*& out) {
    auto* dev = new DxDeviceImpl();
    if (dev->init(this, desc) != ErrorCode::Ok) {
        delete dev; out = nullptr; return ErrorCode::Unknown;
    }
    out = dev;
    return ErrorCode::Ok;
}

// ---- CommandEncoder out-of-line: blitSubresource (needs DxDeviceImpl) ----

void DxCommandEncoderImpl::blitSubresource(DxTextureImpl* srcTex, u32 srcMip,
    DxTextureImpl* dstTex, u32 dstMip, u32 dstWidth, u32 dstHeight, DXGI_FORMAT dxgiFormat) {

    auto* blitRootSig = device_->blitRootSignature();
    if (!blitRootSig) return;
    auto* blitPso = device_->getOrCreateBlitPSO(dxgiFormat);
    if (!blitPso) return;

    // Allocate temp RTV for destination mip.
    auto rtvHandle = device_->rtvHeap()->allocate();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = dxgiFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = dstMip;
    device_->handle()->CreateRenderTargetView(dstTex->handle(), &rtvDesc, rtvHandle);

    // Allocate temp SRV in CPU heap, write, then stage-copy to GPU heap.
    i32 tempSrvOff = device_->cpuSrvHeap()->allocate(1);
    if (tempSrvOff < 0) { device_->rtvHeap()->free(rtvHandle); return; }

    auto tempCpuHandle = device_->cpuSrvHeap()->getCpuHandle(static_cast<u32>(tempSrvOff));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = dxgiFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = srcMip;
    srvDesc.Texture2D.MipLevels = 1;
    device_->handle()->CreateShaderResourceView(srcTex->handle(), &srvDesc, tempCpuHandle);

    // Copy from CPU heap into GPU staging, then free CPU temp slot.
    i32 stagedOff = pool_->srvStaging()->copyFrom(static_cast<u32>(tempSrvOff), 1);
    device_->cpuSrvHeap()->free(static_cast<u32>(tempSrvOff), 1);
    if (stagedOff < 0) { device_->rtvHeap()->free(rtvHandle); return; }

    auto srvGpuHandle = device_->gpuSrvHeap()->getGpuHandle(static_cast<u32>(stagedOff));

    ensureDescriptorHeaps();

    // Set blit pipeline.
    cmdList_->SetGraphicsRootSignature(blitRootSig);
    cmdList_->SetPipelineState(blitPso);
    cmdList_->SetGraphicsRootDescriptorTable(0, srvGpuHandle);
    cmdList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    D3D12_VIEWPORT vp{}; vp.Width = static_cast<FLOAT>(dstWidth); vp.Height = static_cast<FLOAT>(dstHeight); vp.MaxDepth = 1.0f;
    cmdList_->RSSetViewports(1, &vp);
    D3D12_RECT sc{}; sc.right = static_cast<LONG>(dstWidth); sc.bottom = static_cast<LONG>(dstHeight);
    cmdList_->RSSetScissorRects(1, &sc);

    cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList_->DrawInstanced(3, 1, 0, 0);

    device_->rtvHeap()->free(rtvHandle);
}

// ---- CommandPool out-of-line methods (need DxCommandEncoderImpl + context structs) ----

Status DxCommandPoolImpl::createEncoder(CommandEncoder*& out) {
    out = nullptr;

    ComPtr<ID3D12Device> d3dDev;
    allocator_->GetDevice(IID_PPV_ARGS(&d3dDev));
    if (!d3dDev) return ErrorCode::Unknown;

    ID3D12GraphicsCommandList* cmdList = nullptr;
    HRESULT hr = d3dDev->CreateCommandList(0, type_,
        allocator_.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    if (FAILED(hr)) return ErrorCode::Unknown;

    DxRenderPassContext rpeCtx{};
    rpeCtx.cmdList         = cmdList;
    rpeCtx.srvStaging      = &srvStaging_;
    rpeCtx.samplerStaging  = &samplerStaging_;
    rpeCtx.gpuSrvHeap      = device_->gpuSrvHeap();
    rpeCtx.gpuSamplerHeap  = device_->gpuSamplerHeap();
    rpeCtx.drawSig         = device_->drawSignature();
    rpeCtx.drawIndexedSig  = device_->drawIndexedSignature();
    rpeCtx.dispatchMeshSig = device_->dispatchMeshSignature();

    DxComputePassContext cpeCtx{};
    cpeCtx.cmdList         = cmdList;
    cpeCtx.srvStaging      = &srvStaging_;
    cpeCtx.samplerStaging  = &samplerStaging_;
    cpeCtx.gpuSrvHeap      = device_->gpuSrvHeap();
    cpeCtx.gpuSamplerHeap  = device_->gpuSamplerHeap();
    cpeCtx.dispatchSig     = device_->dispatchSignature();

    auto* enc = new DxCommandEncoderImpl(device_, cmdList, this, rpeCtx, cpeCtx);
    out = enc;
    return ErrorCode::Ok;
}

void DxCommandPoolImpl::destroyEncoder(CommandEncoder*& encoder) {
    if (auto* dx = dynamic_cast<DxCommandEncoderImpl*>(encoder)) delete dx;
    encoder = nullptr;
}

} // namespace raptor::rhi::dx12
