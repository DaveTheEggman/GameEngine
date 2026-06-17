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
    raptor::core::StringView Title() const override { return u"Sample014 - Blit (Scaled Copy)"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = float4(i.Position,1); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return i.Color; }
    )";
    static constexpr raptor::core::u32 kOffscreenSize = 128;

    void updateTriangle();

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr;
    dr::PipelineLayout *m_pl = nullptr; dr::RenderPipeline *m_pipeline = nullptr;
    dr::Texture *m_offscreenTex = nullptr; dr::TextureView *m_offscreenView = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
};

raptor::core::Status BlitSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Triangle VB (CpuToGpu for per-frame rotation updates).
    dr::BufferDesc vbd{}; vbd.size = 84; vbd.usage = dr::BufferUsage::Vertex; vbd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::PipelineLayoutDesc pld{};
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Offscreen render target.
    dr::TextureDesc td{}; td.format = m_swapChain->Format(); td.width = kOffscreenSize; td.height = kOffscreenSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::CopySrc | dr::TextureUsage::Sampled;
    if (m_device->CreateTexture(td, m_offscreenTex) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TextureViewDesc tvd{}; tvd.format = m_swapChain->Format(); tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (m_device->CreateTextureView(m_offscreenTex, tvd, m_offscreenView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void BlitSample::updateTriangle() {
    float angle = m_totalTime * 2.0f;
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
    void* mapped = m_vb->Map();
    if (mapped) { std::memcpy(mapped, verts, 84); m_vb->Unmap(); }
}

void BlitSample::OnRender() {
    using raptor::core::f32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    updateTriangle();

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Pass 1: Render spinning triangle to offscreen texture.
    enc->TransitionTexture(m_offscreenTex, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = m_offscreenView;
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.15f, 0.1f, 0.2f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->BeginRenderPass(rpd);
        rp->SetPipeline(m_pipeline);
        rp->SetViewport(0, 0, static_cast<f32>(kOffscreenSize), static_cast<f32>(kOffscreenSize), 0, 1);
        rp->SetScissor(0, 0, kOffscreenSize, kOffscreenSize);
        rp->SetVertexBuffer(0, m_vb, 0);
        rp->Draw(3);
        rp->End();
    }
    enc->TransitionTexture(m_offscreenTex, dr::ResourceState::RenderTarget, dr::ResourceState::CopySrc);

    // Pass 2: Blit offscreen (128x128) to full swapchain (scaled up with linear filtering).
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::CopyDst);
    enc->Blit(m_offscreenTex, m_swapChain->CurrentTexture());
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::CopyDst, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void BlitSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_offscreenView) m_device->DestroyTextureView(m_offscreenView);
    if (m_offscreenTex) m_device->DestroyTexture(m_offscreenTex);
    if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { BlitSample app; return app.Run(argc, argv); }
