#include <new>
/// Sample014 — Blit (Scaled Copy). Ported from Sedulous Sample014_Blit.
/// Renders a spinning triangle to a small 128x128 offscreen texture, then blits
/// it to the full swapchain (scaled up with linear filtering).

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

class BlitSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample014 - Blit (Scaled Copy)"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = float4(i.Position,1); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return i.Color; }
    )";
    static constexpr raptor::core::u32 kOffscreenSize = 128;

    void updateTriangle();

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr; dr::RenderPipeline *pipeline_ = nullptr;
    dr::Texture *offscreenTex_ = nullptr; dr::TextureView *offscreenView_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status BlitSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Triangle VB (CpuToGpu for per-frame rotation updates).
    dr::BufferDesc vbd{}; vbd.size = 84; vbd.usage = dr::BufferUsage::Vertex; vbd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::PipelineLayoutDesc pld{};
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Offscreen render target.
    dr::TextureDesc td{}; td.format = swapChain_->Format(); td.width = kOffscreenSize; td.height = kOffscreenSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::CopySrc | dr::TextureUsage::Sampled;
    if (device_->CreateTexture(td, offscreenTex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TextureViewDesc tvd{}; tvd.format = swapChain_->Format(); tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (device_->CreateTextureView(offscreenTex_, tvd, offscreenView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void BlitSample::updateTriangle() {
    float angle = totalTime_ * 2.0f;
    float c = std::cos(angle), s = std::sin(angle);
    float basePos[6] = { 0.0f, 0.5f, 0.433f, -0.25f, -0.433f, -0.25f };
    float colors[12] = { 1,0.2f,0.2f,1, 0.2f,1,0.2f,1, 0.2f,0.4f,1,1 };
    float verts[21];
    for (int i = 0; i < 3; ++i) {
        float x = basePos[i*2], y = basePos[i*2+1];
        verts[i*7+0] = x*c - y*s; verts[i*7+1] = x*s + y*c; verts[i*7+2] = 0.0f;
        verts[i*7+3] = colors[i*4]; verts[i*7+4] = colors[i*4+1];
        verts[i*7+5] = colors[i*4+2]; verts[i*7+6] = colors[i*4+3];
    }
    void* mapped = vb_->Map();
    if (mapped) { std::memcpy(mapped, verts, 84); vb_->Unmap(); }
}

void BlitSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    updateTriangle();

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Pass 1: Render spinning triangle to offscreen texture.
    enc->TransitionTexture(offscreenTex_, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = offscreenView_;
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.15f, 0.1f, 0.2f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->BeginRenderPass(rpd);
        rp->SetPipeline(pipeline_);
        rp->SetViewport(0, 0, static_cast<f32>(kOffscreenSize), static_cast<f32>(kOffscreenSize), 0, 1);
        rp->SetScissor(0, 0, kOffscreenSize, kOffscreenSize);
        rp->SetVertexBuffer(0, vb_, 0);
        rp->Draw(3);
        rp->End();
    }
    enc->TransitionTexture(offscreenTex_, dr::ResourceState::RenderTarget, dr::ResourceState::CopySrc);

    // Pass 2: Blit offscreen (128x128) to full swapchain (scaled up with linear filtering).
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::CopyDst);
    enc->Blit(offscreenTex_, swapChain_->CurrentTexture());
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::CopyDst, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void BlitSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_); if (pl_) device_->DestroyPipelineLayout(pl_);
    if (offscreenView_) device_->DestroyTextureView(offscreenView_);
    if (offscreenTex_) device_->DestroyTexture(offscreenTex_);
    if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { BlitSample app; return app.run(argc, argv); }
