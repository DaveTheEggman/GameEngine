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
    raptor::core::WideStringView Title() const override { return u"Sample010 - MSAA (4x)"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateMSAA(w, h); }
    void OnShutdown() override;
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

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr;
    dr::PipelineLayout *m_pl = nullptr; dr::RenderPipeline *m_pipeline = nullptr;
    dr::Texture *m_msaaTex = nullptr; dr::TextureView *m_msaaView = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
};

void MSAASample::recreateMSAA(raptor::core::u32 w, raptor::core::u32 h) {
    if (m_msaaView) { m_device->DestroyTextureView(m_msaaView); m_msaaView = nullptr; }
    if (m_msaaTex) { m_device->DestroyTexture(m_msaaTex); m_msaaTex = nullptr; }
    dr::TextureDesc td = dr::TextureDesc::RenderTarget(m_swapChain->Format(), w, h, kSamples, u"MSAATarget");
    m_device->CreateTexture(td, m_msaaTex);
    dr::TextureViewDesc tvd{}; tvd.format = m_swapChain->Format(); tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    m_device->CreateTextureView(m_msaaTex, tvd, m_msaaView);
}

raptor::core::Status MSAASample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{}; if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.multisample.count = kSamples;
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    recreateMSAA(m_width, m_height);
    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void MSAASample::OnRender() {
    using raptor::core::f32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(m_msaaTex, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    // Render into MSAA target, resolve to swap chain.
    dr::ColorAttachment ca{};
    ca.view = m_msaaView;
    ca.resolveTarget = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(m_pipeline);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    rp->SetVertexBuffer(0, m_vb, 0); rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(3); rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue); m_pool->DestroyEncoder(enc);
}

void MSAASample::OnShutdown() {
    if (m_msaaView) m_device->DestroyTextureView(m_msaaView); if (m_msaaTex) m_device->DestroyTexture(m_msaaTex);
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { MSAASample app; return app.Run(argc, argv); }
