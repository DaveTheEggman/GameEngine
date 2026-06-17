#include <new>
/// Sample017 — MultiQueue (Async Compute). Ported from Sedulous Sample017_MultiQueue.
/// A compute shader generates an animated vertex grid on the compute queue,
/// then the graphics queue waits on the compute fence and renders the result.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;
using raptor::core::Mat4;

class MultiQueueSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample017 - MultiQueue (Async Compute)"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateDepth(w, h); }
    void onShutdown() override;
private:
    static constexpr const char8_t kComputeSrc[] = u8R"(
        cbuffer Params : register(b0, space0) { float Time; uint NumPoints; float Spacing; float Padding; };
        struct Vertex { float PosX, PosY, PosZ, ColR, ColG, ColB; };
        RWStructuredBuffer<Vertex> gVertices : register(u0, space0);
        [numthreads(64, 1, 1)]
        void CSMain(uint3 dtid : SV_DispatchThreadID) {
            uint idx = dtid.x; if (idx >= NumPoints) return;
            uint gridSize = (uint)sqrt((float)NumPoints);
            uint row = idx / gridSize, col = idx % gridSize;
            float fx = ((float)col / (float)(gridSize-1))*2.0 - 1.0;
            float fz = ((float)row / (float)(gridSize-1))*2.0 - 1.0;
            float dist = sqrt(fx*fx + fz*fz);
            float fy = sin(dist*8.0 - Time*3.0) * 0.2;
            gVertices[idx].PosX = fx; gVertices[idx].PosY = fy; gVertices[idx].PosZ = fz;
            gVertices[idx].ColR = 0.5 + 0.5*sin(Time + fx*3.0);
            gVertices[idx].ColG = 0.5 + 0.5*cos(Time + fz*3.0);
            gVertices[idx].ColB = 0.5 + 0.5*sin(Time*0.7 + dist*4.0);
        }
    )";
    static constexpr const char8_t kRenderSrc[] = u8R"(
        cbuffer ViewProj : register(b0, space0) { row_major float4x4 VP; };
        struct VSInput { float3 Position : TEXCOORD0; float3 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float3 Color : COLOR0;
                         [[vk::builtin("PointSize")]] float PointSize : PSIZE; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = mul(float4(i.Position,1), VP); o.Color = i.Color; o.PointSize = 1.0; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return float4(i.Color, 1.0); }
    )";

    static constexpr raptor::core::u32 kGrid = 64, kNumPts = kGrid*kGrid, kVertSz = 24, kBufSz = kNumPts*kVertSz;

    void recreateDepth(raptor::core::u32 w, raptor::core::u32 h);

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *cs_ = nullptr, *vs_ = nullptr, *ps_ = nullptr;

    // Compute resources.
    dr::Queue* computeQueue_ = nullptr;
    dr::CommandPool *computePool_ = nullptr;
    dr::BindGroupLayout *compBgl_ = nullptr; dr::BindGroup *compBg_ = nullptr;
    dr::PipelineLayout *compPl_ = nullptr; dr::ComputePipeline *compPipe_ = nullptr;
    dr::Buffer *paramsBuf_ = nullptr; void *paramsMapped_ = nullptr;

    // Graphics resources.
    dr::CommandPool *gfxPool_ = nullptr;
    dr::BindGroupLayout *renBgl_ = nullptr; dr::BindGroup *renBg_ = nullptr;
    dr::PipelineLayout *renPl_ = nullptr; dr::RenderPipeline *renPipe_ = nullptr;
    dr::Buffer *vpBuf_ = nullptr; void *vpMapped_ = nullptr;

    // Shared.
    dr::Buffer *vtxBuf_ = nullptr;
    sf::DepthBuffer depthBuf_;

    // Synchronization.
    dr::Fence *compFence_ = nullptr, *gfxFence_ = nullptr;
    raptor::core::u64 compFenceVal_ = 0, gfxFenceVal_ = 0;
    bool hasDedicatedCompute_ = false;
    float lastReportTime_ = 0.0f;
};

void MultiQueueSample::recreateDepth(raptor::core::u32 w, raptor::core::u32 h) {
    depthBuf_.recreate(device_, w, h);
}

raptor::core::Status MultiQueueSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    // Check for dedicated compute queue.
    if (device_->GetQueueCount(dr::QueueType::Compute) == 0) {
        computeQueue_ = graphicsQueue_;
        hasDedicatedCompute_ = false;
        std::printf("No dedicated compute queue - using graphics queue for both\n");
    } else {
        computeQueue_ = device_->GetQueue(dr::QueueType::Compute, 0);
        hasDedicatedCompute_ = true;
        std::printf("Using dedicated compute queue\n");
    }

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kComputeSrc, ds::ShaderStage::Compute,  u"CSMain", u"CS", cs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kRenderSrc, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kRenderSrc, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Shared vertex/storage buffer.
    dr::BufferDesc vbd{}; vbd.size = kBufSz; vbd.usage = dr::BufferUsage::Storage | dr::BufferUsage::Vertex; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vtxBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Compute params UBO.
    dr::BufferDesc pbd{}; pbd.size = 16; pbd.usage = dr::BufferUsage::Uniform; pbd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(pbd, paramsBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    paramsMapped_ = paramsBuf_->Map();

    // View-projection UBO.
    dr::BufferDesc vpd{}; vpd.size = 64; vpd.usage = dr::BufferUsage::Uniform; vpd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(vpd, vpBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    vpMapped_ = vpBuf_->Map();

    // Compute pipeline.
    dr::BindGroupLayoutEntry cE[2] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Compute),
                                        dr::BindGroupLayoutEntry::StorageBuffer(0, dr::ShaderStage::Compute, false) };
    dr::BindGroupLayoutDesc cBgld{}; cBgld.entries = Span<const dr::BindGroupLayoutEntry>(cE, 2);
    if (device_->CreateBindGroupLayout(cBgld, compBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry cBgE[2] = { dr::BindGroupEntry::BufferEntry(paramsBuf_, 0, 16),
                                    dr::BindGroupEntry::BufferEntry(vtxBuf_, 0, kBufSz) };
    dr::BindGroupDesc cBgd{}; cBgd.layout = compBgl_; cBgd.entries = Span<const dr::BindGroupEntry>(cBgE, 2);
    if (device_->CreateBindGroup(cBgd, compBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* cSets[1] = { compBgl_ };
    dr::PipelineLayoutDesc cPld{}; cPld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(cSets, 1);
    if (device_->CreatePipelineLayout(cPld, compPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::ComputePipelineDesc cpd{}; cpd.layout = compPl_; cpd.compute = { cs_, u"CSMain", dr::ShaderStage::Compute };
    if (device_->CreateComputePipeline(cpd, compPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline.
    dr::BindGroupLayoutEntry rE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc rBgld{}; rBgld.entries = Span<const dr::BindGroupLayoutEntry>(rE, 1);
    if (device_->CreateBindGroupLayout(rBgld, renBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry rBgE[1] = { dr::BindGroupEntry::BufferEntry(vpBuf_, 0, 64) };
    dr::BindGroupDesc rBgd{}; rBgd.layout = renBgl_; rBgd.entries = Span<const dr::BindGroupEntry>(rBgE, 1);
    if (device_->CreateBindGroup(rBgd, renBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* rSets[1] = { renBgl_ };
    dr::PipelineLayoutDesc rPld{}; rPld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(rSets, 1);
    if (device_->CreatePipelineLayout(rPld, renPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    depthBuf_.recreate(device_, width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = kVertSz; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = renPl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::PointList;
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthWriteEnabled = true; rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (device_->CreateRenderPipeline(rpd, renPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Command pools — one per queue type.
    if (device_->CreateCommandPool(dr::QueueType::Graphics, gfxPool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    auto compPoolType = hasDedicatedCompute_ ? dr::QueueType::Compute : dr::QueueType::Graphics;
    if (device_->CreateCommandPool(compPoolType, computePool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateFence(0, compFence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, gfxFence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void MultiQueueSample::onRender() {
    using raptor::core::u32, raptor::core::f32, raptor::core::Span;
    if (gfxFenceVal_ > 0) gfxFence_->Wait(gfxFenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update compute params.
    u32 numPts = kNumPts;
    f32 params[4] = { totalTime_, 0, 1.0f, 0 };
    std::memcpy(&params[1], &numPts, 4);
    std::memcpy(paramsMapped_, params, 16);

    // Update VP.
    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);
    f32 camAngle = totalTime_ * 0.4f, camDist = 2.5f;
    Mat4 view = Mat4::LookAtRH(raptor::core::Vec3{std::sin(camAngle)*camDist, 1.2f, std::cos(camAngle)*camDist}, raptor::core::Vec3{ 0,0,0}, raptor::core::Vec3{0,1,0});
    Mat4 proj = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Mat4 vp = view * proj;
    std::memcpy(vpMapped_, vp.Data(), 64);

    // === Compute pass on compute queue ===
    computePool_->Reset();
    dr::CommandEncoder* cEnc = nullptr;
    if (computePool_->CreateEncoder(cEnc) != raptor::core::ErrorCode::Ok || !cEnc) return;

    dr::BufferBarrier bb{}; bb.buffer = vtxBuf_; bb.oldState = dr::ResourceState::VertexBuffer; bb.newState = dr::ResourceState::ShaderWrite;
    dr::BarrierGroup bg1{}; bg1.bufferBarriers = Span<const dr::BufferBarrier>(&bb, 1);
    cEnc->Barrier(bg1);
    auto* cp = cEnc->BeginComputePass(u"AsyncCompute");
    cp->SetPipeline(compPipe_); cp->SetBindGroup(0, compBg_);
    cp->Dispatch((kNumPts + 63) / 64); cp->End();
    bb.oldState = dr::ResourceState::ShaderWrite; bb.newState = dr::ResourceState::VertexBuffer;
    cEnc->Barrier(bg1);

    dr::CommandBuffer* cCb = cEnc->Finish(); compFenceVal_++;
    dr::CommandBuffer* cCbs[1] = { cCb };
    computeQueue_->Submit(Span<dr::CommandBuffer* const>(cCbs, 1), compFence_, compFenceVal_);
    computePool_->DestroyEncoder(cEnc);

    // === Graphics pass — waits on compute fence before executing ===
    gfxPool_->Reset();
    dr::CommandEncoder* gEnc = nullptr;
    if (gfxPool_->CreateEncoder(gEnc) != raptor::core::ErrorCode::Ok || !gEnc) return;

    gEnc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    gEnc->TransitionTexture(depthBuf_.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.03f, 0.03f, 0.06f, 1.0f);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthBuf_.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = gEnc->BeginRenderPass(rpd);
    rp->SetPipeline(renPipe_); rp->SetBindGroup(0, renBg_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vtxBuf_, 0);
    rp->Draw(kNumPts); rp->End();

    gEnc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* gCb = gEnc->Finish(); gfxFenceVal_++;
    dr::CommandBuffer* gCbs[1] = { gCb };

    // Submit graphics — wait on compute fence, signal graphics fence.
    dr::Fence* waitFences[1] = { compFence_ };
    raptor::core::u64 waitValues[1] = { compFenceVal_ };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(gCbs, 1),
                           Span<dr::Fence* const>(waitFences, 1),
                           Span<const raptor::core::u64>(waitValues, 1),
                           gfxFence_, gfxFenceVal_);
    swapChain_->Present(graphicsQueue_);
    gfxPool_->DestroyEncoder(gEnc);

    if (totalTime_ - lastReportTime_ >= 3.0f) {
        std::printf("MultiQueue: compute fence=%llu, graphics fence=%llu, dt=%.2fms\n",
            static_cast<unsigned long long>(compFenceVal_),
            static_cast<unsigned long long>(gfxFenceVal_),
            deltaTime_ * 1000.0f);
        lastReportTime_ = totalTime_;
    }
}

void MultiQueueSample::onShutdown() {
    if (paramsBuf_ && paramsMapped_) paramsBuf_->Unmap();
    if (vpBuf_ && vpMapped_) vpBuf_->Unmap();
    depthBuf_.destroy(device_);
    if (gfxFence_) device_->DestroyFence(gfxFence_); if (compFence_) device_->DestroyFence(compFence_);
    if (gfxPool_) device_->DestroyCommandPool(gfxPool_); if (computePool_) device_->DestroyCommandPool(computePool_);
    if (renPipe_) device_->DestroyRenderPipeline(renPipe_); if (renPl_) device_->DestroyPipelineLayout(renPl_);
    if (renBg_) device_->DestroyBindGroup(renBg_); if (renBgl_) device_->DestroyBindGroupLayout(renBgl_);
    if (compPipe_) device_->DestroyComputePipeline(compPipe_); if (compPl_) device_->DestroyPipelineLayout(compPl_);
    if (compBg_) device_->DestroyBindGroup(compBg_); if (compBgl_) device_->DestroyBindGroupLayout(compBgl_);
    if (vpBuf_) device_->DestroyBuffer(vpBuf_); if (paramsBuf_) device_->DestroyBuffer(paramsBuf_);
    if (vtxBuf_) device_->DestroyBuffer(vtxBuf_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (cs_) device_->DestroyShaderModule(cs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { MultiQueueSample app; return app.run(argc, argv); }
