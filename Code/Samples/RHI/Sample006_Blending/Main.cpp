#include <new>
/// Sample006 — Alpha Blending. Ported from Sedulous Sample006_Blending.
/// Renders overlapping semi-transparent colored quads.

#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class BlendingSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample006 - Alpha Blending"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = float4(i.Position, 1.0); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return i.Color; }
    )";
    // 4 quads: background opaque + 3 overlapping translucent.
    static constexpr float kVerts[] = {
        -.9f,-.9f,.5f, .15f,.15f,.2f,1, .9f,-.9f,.5f, .15f,.15f,.2f,1, .9f,.9f,.5f, .15f,.15f,.2f,1, -.9f,.9f,.5f, .15f,.15f,.2f,1,
        -.6f,-.4f,.3f, 1,.2f,.2f,.5f, .1f,-.4f,.3f, 1,.2f,.2f,.5f, .1f,.4f,.3f, 1,.2f,.2f,.5f, -.6f,.4f,.3f, 1,.2f,.2f,.5f,
        -.3f,-.5f,.2f, .2f,1,.2f,.5f, .4f,-.5f,.2f, .2f,1,.2f,.5f, .4f,.3f,.2f, .2f,1,.2f,.5f, -.3f,.3f,.2f, .2f,1,.2f,.5f,
        -.1f,-.3f,.1f, .2f,.3f,1,.5f, .6f,-.3f,.1f, .2f,.3f,1,.5f, .6f,.5f,.1f, .2f,.3f,1,.5f, -.1f,.5f,.1f, .2f,.3f,1,.5f,
    };
    static constexpr raptor::core::u16 kIdx[] = { 0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11, 12,13,14,12,14,15 };

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *opaquePipe_ = nullptr, *blendPipe_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status BlendingSample::onInit() {
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

    // Empty pipeline layout.
    dr::PipelineLayoutDesc pld{};
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ctOpaque{}; ctOpaque.format = swapChain_->format();
    dr::ColorTargetState ctBlend{}; ctBlend.format = swapChain_->format();
    ctBlend.blend = dr::BlendState::alphaBlend();

    // Opaque pipeline (for background quad).
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ctOpaque, 1);
    if (device_->createRenderPipeline(rpd, opaquePipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Blend pipeline (for translucent quads).
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ctBlend, 1);
    if (device_->createRenderPipeline(rpd, blendPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void BlendingSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->beginRenderPass(rpd);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0);
    rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    // Draw background opaque.
    rp->setPipeline(opaquePipe_); rp->drawIndexed(6, 1, 0, 0, 0);
    // Draw 3 translucent quads.
    rp->setPipeline(blendPipe_); rp->drawIndexed(18, 1, 6, 0, 0);
    rp->end();
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_); pool_->destroyEncoder(enc);
}

void BlendingSample::onShutdown() {
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (blendPipe_) device_->destroyRenderPipeline(blendPipe_);
    if (opaquePipe_) device_->destroyRenderPipeline(opaquePipe_);
    if (pl_) device_->destroyPipelineLayout(pl_);
    if (ib_) device_->destroyBuffer(ib_); if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { BlendingSample app; return app.run(argc, argv); }
