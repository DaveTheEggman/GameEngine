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
    raptor::core::WideStringView Title() const override { return u"Sample006 - Alpha Blending"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
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

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr;
    dr::PipelineLayout *m_pl = nullptr;
    dr::RenderPipeline *m_opaquePipe = nullptr, *m_blendPipe = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
};

raptor::core::Status BlendingSample::OnInit() {
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

    // Empty pipeline layout.
    dr::PipelineLayoutDesc pld{};
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ctOpaque{}; ctOpaque.format = m_swapChain->Format();
    dr::ColorTargetState ctBlend{}; ctBlend.format = m_swapChain->Format();
    ctBlend.blend = dr::BlendState::AlphaBlend();

    // Opaque pipeline (for background quad).
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ctOpaque, 1);
    if (m_device->CreateRenderPipeline(rpd, m_opaquePipe) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Blend pipeline (for translucent quads).
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ctBlend, 1);
    if (m_device->CreateRenderPipeline(rpd, m_blendPipe) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void BlendingSample::OnRender() {
    using raptor::core::f32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    rp->SetVertexBuffer(0, m_vb, 0);
    rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    // Draw background opaque.
    rp->SetPipeline(m_opaquePipe); rp->DrawIndexed(6, 1, 0, 0, 0);
    // Draw 3 translucent quads.
    rp->SetPipeline(m_blendPipe); rp->DrawIndexed(18, 1, 6, 0, 0);
    rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue); m_pool->DestroyEncoder(enc);
}

void BlendingSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_blendPipe) m_device->DestroyRenderPipeline(m_blendPipe);
    if (m_opaquePipe) m_device->DestroyRenderPipeline(m_opaquePipe);
    if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { BlendingSample app; return app.Run(argc, argv); }
