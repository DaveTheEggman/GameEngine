#include <new>
/// Sample008 — Depth Buffer. Ported from Sedulous Sample008_DepthBuffer.
/// Three overlapping quads at different Z depths demonstrate depth testing.
/// Red (z=0.8, far), Green (z=0.5, mid), Blue (z=0.2, near) — drawn far-to-near,
/// depth buffer ensures correct visibility.

#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class DepthBufferSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample008 - Depth Buffer"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateDepth(w, h); }
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput
        {
            float3 Position : TEXCOORD0;
            float4 Color    : TEXCOORD1;
        };
        struct PSInput
        {
            float4 Position : SV_POSITION;
            float4 Color    : COLOR0;
        };
        PSInput VSMain(VSInput input)
        {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.Color = input.Color;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET
        {
            return input.Color;
        }
    )";

    // Three overlapping quads drawn in order: red (far), green (middle), blue (near).
    // Stride: 7 floats per vertex (pos xyz + color rgba).
    static constexpr float kVerts[] = {
        // Quad 0: Red — large, behind (z=0.8), drawn first.
        -0.6f, -0.6f, 0.8f,   1.0f, 0.2f, 0.2f, 1.0f,
         0.4f, -0.6f, 0.8f,   1.0f, 0.2f, 0.2f, 1.0f,
         0.4f,  0.6f, 0.8f,   1.0f, 0.2f, 0.2f, 1.0f,
        -0.6f,  0.6f, 0.8f,   1.0f, 0.2f, 0.2f, 1.0f,
        // Quad 1: Green — overlaps red, closer (z=0.5), drawn second.
        -0.2f, -0.4f, 0.5f,   0.2f, 1.0f, 0.2f, 1.0f,
         0.6f, -0.4f, 0.5f,   0.2f, 1.0f, 0.2f, 1.0f,
         0.6f,  0.4f, 0.5f,   0.2f, 1.0f, 0.2f, 1.0f,
        -0.2f,  0.4f, 0.5f,   0.2f, 1.0f, 0.2f, 1.0f,
        // Quad 2: Blue — overlaps both, nearest (z=0.2), drawn third.
        -0.4f, -0.7f, 0.2f,   0.2f, 0.3f, 1.0f, 1.0f,
         0.2f, -0.7f, 0.2f,   0.2f, 0.3f, 1.0f, 1.0f,
         0.2f,  0.7f, 0.2f,   0.2f, 0.3f, 1.0f, 1.0f,
        -0.4f,  0.7f, 0.2f,   0.2f, 0.3f, 1.0f, 1.0f,
    };
    static constexpr raptor::core::u16 kIdx[] = {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
    };

    void recreateDepth(raptor::core::u32 w, raptor::core::u32 h);

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *pipeline_ = nullptr;
    dr::Texture *depthTex_ = nullptr;
    dr::TextureView *depthView_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

void DepthBufferSample::recreateDepth(raptor::core::u32 w, raptor::core::u32 h) {
    if (depthView_) { device_->destroyTextureView(depthView_); depthView_ = nullptr; }
    if (depthTex_) { device_->destroyTexture(depthTex_); depthTex_ = nullptr; }

    dr::TextureDesc td = dr::TextureDesc::depthBuffer(dr::TextureFormat::Depth24PlusStencil8, w, h, 1, u"DepthTex");
    device_->createTexture(td, depthTex_);
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::Depth24PlusStencil8; tvd.dimension = dr::TextureViewDimension::Texture2D;
    tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    device_->createTextureView(depthTex_, tvd, depthView_);
}

raptor::core::Status DepthBufferSample::onInit() {
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

    // Pipeline layout (empty — no bind groups needed).
    dr::PipelineLayoutDesc pld{};
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    recreateDepth(width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthWriteEnabled = true;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void DepthBufferSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->transitionTexture(depthTex_, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthView_;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->beginRenderPass(rpd);

    rp->setPipeline(pipeline_);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0);
    rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    // Draw all 3 quads — depth buffer determines visibility.
    rp->drawIndexed(18);
    rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void DepthBufferSample::onShutdown() {
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_); if (pl_) device_->destroyPipelineLayout(pl_);
    if (depthView_) device_->destroyTextureView(depthView_);
    if (depthTex_) device_->destroyTexture(depthTex_);
    if (ib_) device_->destroyBuffer(ib_); if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { DepthBufferSample app; return app.run(argc, argv); }
