#include <new>
/// Sample004 — Compute Shader (Animated Point Grid).
/// Ported from Sedulous Sample004_Compute.

#include <cmath>
#include <cstdint>
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

class ComputeSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample004 - Compute (Animated Point Grid)"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { depthBuf_.recreate(device_, w, h); }
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
            float fy = sin(dist*6.0 - Time*2.0) * 0.15;
            gVertices[idx].PosX = fx; gVertices[idx].PosY = fy; gVertices[idx].PosZ = fz;
            gVertices[idx].ColR = fx*0.5+0.5; gVertices[idx].ColG = fy*2.0+0.5; gVertices[idx].ColB = fz*0.5+0.5;
        }
    )";
    static constexpr const char8_t kRenderSrc[] = u8R"(
        cbuffer ViewProj : register(b0, space0) { row_major float4x4 VP; };
        struct VSInput { float3 Position : TEXCOORD0; float3 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float3 Color : COLOR0;
                         [[vk::builtin("PointSize")]] float PointSize : PSIZE; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = mul(VP, float4(i.Position,1)); o.Color = i.Color; o.PointSize = 1.0; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return float4(i.Color, 1.0); }
    )";

    static constexpr raptor::core::u32 kGrid = 64, kNumPts = kGrid*kGrid, kVertSz = 24, kBufSz = kNumPts*kVertSz;

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *cs_ = nullptr, *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vtxBuf_ = nullptr, *paramsBuf_ = nullptr, *vpBuf_ = nullptr;
    void *paramsMapped_ = nullptr, *vpMapped_ = nullptr;
    dr::BindGroupLayout *compBgl_ = nullptr, *renBgl_ = nullptr;
    dr::BindGroup *compBg_ = nullptr, *renBg_ = nullptr;
    dr::PipelineLayout *compPl_ = nullptr, *renPl_ = nullptr;
    dr::ComputePipeline *compPipe_ = nullptr;
    dr::RenderPipeline *renPipe_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    sf::DepthBuffer depthBuf_;
};

raptor::core::Status ComputeSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kComputeSrc, ds::ShaderStage::Compute, u"CSMain", u"CS", cs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kRenderSrc, ds::ShaderStage::Vertex,  u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kRenderSrc, ds::ShaderStage::Fragment,u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = kBufSz; vbd.usage = dr::BufferUsage::Storage | dr::BufferUsage::Vertex; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(vbd, vtxBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc pbd{}; pbd.size = 16; pbd.usage = dr::BufferUsage::Uniform; pbd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->createBuffer(pbd, paramsBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    paramsMapped_ = paramsBuf_->map();
    dr::BufferDesc vpd{}; vpd.size = 64; vpd.usage = dr::BufferUsage::Uniform; vpd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->createBuffer(vpd, vpBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    vpMapped_ = vpBuf_->map();

    // Compute BGL + BG + PL + pipeline.
    dr::BindGroupLayoutEntry cE[2] = { dr::BindGroupLayoutEntry::uniformBuffer(0, dr::ShaderStage::Compute),
                                        dr::BindGroupLayoutEntry::storageBuffer(0, dr::ShaderStage::Compute, false) };
    dr::BindGroupLayoutDesc cBgld{}; cBgld.entries = Span<const dr::BindGroupLayoutEntry>(cE, 2);
    if (device_->createBindGroupLayout(cBgld, compBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry cBgE[2] = { dr::BindGroupEntry::bufferEntry(paramsBuf_, 0, 16),
                                    dr::BindGroupEntry::bufferEntry(vtxBuf_, 0, kBufSz) };
    dr::BindGroupDesc cBgd{}; cBgd.layout = compBgl_; cBgd.entries = Span<const dr::BindGroupEntry>(cBgE, 2);
    if (device_->createBindGroup(cBgd, compBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* cSets[1] = { compBgl_ };
    dr::PipelineLayoutDesc cPld{}; cPld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(cSets, 1);
    if (device_->createPipelineLayout(cPld, compPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::ComputePipelineDesc cpd{}; cpd.layout = compPl_; cpd.compute = { cs_, u"CSMain", dr::ShaderStage::Compute };
    if (device_->createComputePipeline(cpd, compPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render BGL + BG + PL + pipeline.
    dr::BindGroupLayoutEntry rE[1] = { dr::BindGroupLayoutEntry::uniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc rBgld{}; rBgld.entries = Span<const dr::BindGroupLayoutEntry>(rE, 1);
    if (device_->createBindGroupLayout(rBgld, renBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry rBgE[1] = { dr::BindGroupEntry::bufferEntry(vpBuf_, 0, 64) };
    dr::BindGroupDesc rBgd{}; rBgd.layout = renBgl_; rBgd.entries = Span<const dr::BindGroupEntry>(rBgE, 1);
    if (device_->createBindGroup(rBgd, renBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* rSets[1] = { renBgl_ };
    dr::PipelineLayoutDesc rPld{}; rPld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(rSets, 1);
    if (device_->createPipelineLayout(rPld, renPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    depthBuf_.recreate(device_, width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = kVertSz; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = renPl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::PointList;
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (device_->createRenderPipeline(rpd, renPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void ComputeSample::onRender() {
    using raptor::core::u32, raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update params.
    u32 numPts = kNumPts;
    f32 params[4] = { totalTime_, 0, 1.0f, 0 };
    std::memcpy(&params[1], &numPts, 4);
    std::memcpy(paramsMapped_, params, 16);

    // Update VP.
    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);
    f32 camAngle = totalTime_ * 0.3f, camDist = 2.5f;
    Mat4 view = Mat4::LookAtRH(raptor::core::Vec3{std::sin(camAngle)*camDist, 1.2f, std::cos(camAngle)*camDist}, raptor::core::Vec3{ 0,0,0}, raptor::core::Vec3{0,1,0});
    Mat4 proj = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Mat4 vp = proj * view;
    std::memcpy(vpMapped_, vp.Data(), 64);

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Compute pass.
    dr::BufferBarrier bb{}; bb.buffer = vtxBuf_; bb.oldState = dr::ResourceState::VertexBuffer; bb.newState = dr::ResourceState::ShaderWrite;
    dr::BarrierGroup bg1{}; bg1.bufferBarriers = Span<const dr::BufferBarrier>(&bb, 1);
    enc->barrier(bg1);
    auto* cp = enc->beginComputePass(u"GenerateVertices");
    cp->setPipeline(compPipe_); cp->setBindGroup(0, compBg_);
    cp->dispatch((kNumPts + 63) / 64); cp->end();
    bb.oldState = dr::ResourceState::ShaderWrite; bb.newState = dr::ResourceState::VertexBuffer;
    enc->barrier(bg1);

    // Render pass.
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->transitionTexture(depthBuf_.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthBuf_.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->beginRenderPass(rpd);
    rp->setPipeline(renPipe_); rp->setBindGroup(0, renBg_);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vtxBuf_, 0);
    rp->draw(kNumPts); rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void ComputeSample::onShutdown() {
    depthBuf_.destroy(device_);
    if (fence_) device_->destroyFence(fence_);
    if (pool_) device_->destroyCommandPool(pool_);
    if (renPipe_) device_->destroyRenderPipeline(renPipe_);
    if (renPl_) device_->destroyPipelineLayout(renPl_);
    if (renBg_) device_->destroyBindGroup(renBg_);
    if (renBgl_) device_->destroyBindGroupLayout(renBgl_);
    if (compPipe_) device_->destroyComputePipeline(compPipe_);
    if (compPl_) device_->destroyPipelineLayout(compPl_);
    if (compBg_) device_->destroyBindGroup(compBg_);
    if (compBgl_) device_->destroyBindGroupLayout(compBgl_);
    if (vpBuf_) device_->destroyBuffer(vpBuf_);
    if (paramsBuf_) device_->destroyBuffer(paramsBuf_);
    if (vtxBuf_) device_->destroyBuffer(vtxBuf_);
    if (ps_) device_->destroyShaderModule(ps_);
    if (vs_) device_->destroyShaderModule(vs_);
    if (cs_) device_->destroyShaderModule(cs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { ComputeSample app; return app.run(argc, argv); }
