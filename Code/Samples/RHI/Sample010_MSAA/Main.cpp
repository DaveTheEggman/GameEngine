#include <new>
/// Sample010 — MSAA. Ported from Sedulous Sample010_MSAA.
/// Renders a triangle with 4x MSAA, resolving to the swap chain.

#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class MSAASample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample010 - MSAA (4x)"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateMSAA(w, h); }
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float3 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float3 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = float4(i.Position,1); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return float4(i.Color, 1.0); }
    )";
    static constexpr float kVerts[] = { 0,.7f,0, 1,0,0,  .7f,-.5f,0, 0,1,0,  -.7f,-.5f,0, 0,0,1 };
    static constexpr raptor::core::u16 kIdx[] = { 0, 1, 2 };
    static constexpr raptor::core::u32 kSamples = 4;

    void recreateMSAA(raptor::core::u32 w, raptor::core::u32 h);

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr; dr::RenderPipeline *pipeline_ = nullptr;
    dr::Texture *msaaTex_ = nullptr; dr::TextureView *msaaView_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

void MSAASample::recreateMSAA(raptor::core::u32 w, raptor::core::u32 h) {
    if (msaaView_) { device_->destroyTextureView(msaaView_); msaaView_ = nullptr; }
    if (msaaTex_) { device_->destroyTexture(msaaTex_); msaaTex_ = nullptr; }
    dr::TextureDesc td = dr::TextureDesc::renderTarget(swapChain_->format(), w, h, kSamples, u"MSAATarget");
    device_->createTexture(td, msaaTex_);
    dr::TextureViewDesc tvd{}; tvd.format = swapChain_->format(); tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    device_->createTextureView(msaaTex_, tvd, msaaView_);
}

raptor::core::Status MSAASample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->writeBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->submit(); graphicsQueue_->destroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{}; if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.multisample.count = kSamples;
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    recreateMSAA(width_, height_);
    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void MSAASample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->transitionTexture(msaaTex_, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    // Render into MSAA target, resolve to swap chain.
    dr::ColorAttachment ca{};
    ca.view = msaaView_;
    ca.resolveTarget = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->beginRenderPass(rpd);
    rp->setPipeline(pipeline_);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0); rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->drawIndexed(3); rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_); pool_->destroyEncoder(enc);
}

void MSAASample::onShutdown() {
    if (msaaView_) device_->destroyTextureView(msaaView_); if (msaaTex_) device_->destroyTexture(msaaTex_);
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_); if (pl_) device_->destroyPipelineLayout(pl_);
    if (ib_) device_->destroyBuffer(ib_); if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { MSAASample app; return app.run(argc, argv); }
