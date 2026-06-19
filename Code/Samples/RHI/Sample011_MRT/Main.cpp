#include <new>
/// Sample011 — Multiple Render Targets. Ported from Sedulous Sample011_MRT.
/// Pass 1: Renders triangles to 2 render targets (color + brightness).
/// Pass 2: Composites both side-by-side via fullscreen triangle.

#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class MRTSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView Title() const override { return u8"Sample011 - MRT"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnResize(raptor::core::u32 w, raptor::core::u32 h) override { createRenderTargets(); }
    void OnShutdown() override;
private:
    static constexpr const char8_t kGBufShader[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        struct PSOutput { float4 Color : SV_TARGET0; float4 Brightness : SV_TARGET1; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = float4(i.Position,1); o.Color = i.Color; return o; }
        PSOutput PSMain(PSInput i) { PSOutput o; o.Color = i.Color;
            float lum = dot(i.Color.rgb, float3(0.299,0.587,0.114));
            o.Brightness = float4(lum,lum,lum,1); return o; }
    )";
    static constexpr const char8_t kCompShader[] = u8R"(
        Texture2D gColorTex : register(t0, space0);
        Texture2D gBrightTex : register(t1, space0);
        SamplerState gSampler : register(s0, space0);
        struct PSInput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
        PSInput VSMain(uint vid : SV_VertexID) { PSInput o;
            float2 uv = float2((vid << 1) & 2, vid & 2);
            o.Position = float4(uv * 2.0 - 1.0, 0, 1); o.TexCoord = float2(uv.x, 1.0 - uv.y); return o; }
        float4 PSMain(PSInput i) : SV_TARGET {
            float2 uv = i.TexCoord;
            if (uv.x < 0.5) return gColorTex.Sample(gSampler, float2(uv.x*2, uv.y));
            else return gBrightTex.Sample(gSampler, float2((uv.x-0.5)*2, uv.y)); }
    )";
    static constexpr float kVerts[] = {
        -.5f,-.5f,0, 1,.2f,.2f,1,  .5f,-.5f,0, 1,.2f,.2f,1,  0,.6f,0, 1,.8f,.2f,1,
        -.3f,-.3f,0, .2f,.3f,1,1,  .7f,-.1f,0, .2f,.3f,1,1,  .2f,.5f,0, .2f,.8f,1,1,
    };
    static constexpr raptor::core::u16 kIdx[] = { 0,1,2, 3,4,5 };

    void createRenderTargets();

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_gbVs=nullptr, *m_gbPs=nullptr, *m_compVs=nullptr, *m_compPs=nullptr;
    dr::Buffer *m_vb=nullptr, *m_ib=nullptr;
    dr::Sampler* m_sampler = nullptr;
    dr::PipelineLayout *m_gbPl=nullptr, *m_compPl=nullptr;
    dr::RenderPipeline *m_gbPipe=nullptr, *m_compPipe=nullptr;
    dr::BindGroupLayout* m_compBgl = nullptr;
    dr::BindGroup* m_compBg = nullptr;
    dr::Texture *m_colorRT=nullptr, *m_brightRT=nullptr;
    dr::TextureView *m_colorRTView=nullptr, *m_brightRTView=nullptr;
    dr::CommandPool* m_pool=nullptr; dr::Fence* m_fence=nullptr;
    raptor::core::u64 m_fenceVal = 0;
};

void MRTSample::createRenderTargets() {
    if (m_compBg) { m_device->DestroyBindGroup(m_compBg); m_compBg = nullptr; }
    if (m_colorRTView) { m_device->DestroyTextureView(m_colorRTView); m_colorRTView = nullptr; }
    if (m_colorRT) { m_device->DestroyTexture(m_colorRT); m_colorRT = nullptr; }
    if (m_brightRTView) { m_device->DestroyTextureView(m_brightRTView); m_brightRTView = nullptr; }
    if (m_brightRT) { m_device->DestroyTexture(m_brightRT); m_brightRT = nullptr; }

    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = m_width; td.height = m_height;
    td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::Sampled;
    m_device->CreateTexture(td, m_colorRT);
    m_device->CreateTexture(td, m_brightRT);
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    m_device->CreateTextureView(m_colorRT, tvd, m_colorRTView);
    m_device->CreateTextureView(m_brightRT, tvd, m_brightRTView);

    dr::BindGroupEntry bgE[3] = { dr::BindGroupEntry::TextureEntry(m_colorRTView),
                                   dr::BindGroupEntry::TextureEntry(m_brightRTView),
                                   dr::BindGroupEntry::SamplerEntry(m_sampler) };
    dr::BindGroupDesc bgd{}; bgd.layout = m_compBgl; bgd.entries = raptor::core::Span<const dr::BindGroupEntry>(bgE, 3);
    m_device->CreateBindGroup(bgd, m_compBg);
}

raptor::core::Status MRTSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kGBufShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"GBufVS", m_gbVs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kGBufShader, ds::ShaderStage::Fragment, u8"PSMain", u8"GBufPS", m_gbPs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kCompShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"CompVS", m_compVs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kCompShader, ds::ShaderStage::Fragment, u8"PSMain", u8"CompPS", m_compPs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Nearest; sd.magFilter = dr::FilterMode::Nearest;
    sd.addressU = dr::AddressMode::ClampToEdge; sd.addressV = dr::AddressMode::ClampToEdge;
    if (m_device->CreateSampler(sd, m_sampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // GBuffer pipeline (empty layout, 2 color targets).
    dr::PipelineLayoutDesc gpld{}; if (m_device->CreatePipelineLayout(gpld, m_gbPl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState gbCt[2] = { {dr::TextureFormat::RGBA8Unorm}, {dr::TextureFormat::RGBA8Unorm} };
    dr::RenderPipelineDesc grpd{}; grpd.layout = m_gbPl;
    grpd.vertex.shader = { m_gbVs, u8"VSMain", dr::ShaderStage::Vertex };
    grpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    grpd.fragment = dr::FragmentState{}; grpd.fragment->shader = { m_gbPs, u8"PSMain", dr::ShaderStage::Fragment };
    grpd.fragment->targets = Span<const dr::ColorTargetState>(gbCt, 2);
    if (m_device->CreateRenderPipeline(grpd, m_gbPipe) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Composite BGL + pipeline (3 bindings: 2 textures + 1 sampler).
    dr::BindGroupLayoutEntry cE[3] = {
        dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::SampledTexture(1, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment),
    };
    dr::BindGroupLayoutDesc cBgld{}; cBgld.entries = Span<const dr::BindGroupLayoutEntry>(cE, 3);
    if (m_device->CreateBindGroupLayout(cBgld, m_compBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* cSets[1] = { m_compBgl };
    dr::PipelineLayoutDesc cpld{}; cpld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(cSets, 1);
    if (m_device->CreatePipelineLayout(cpld, m_compPl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::ColorTargetState compCt{}; compCt.format = m_swapChain->Format();
    dr::RenderPipelineDesc crpd{}; crpd.layout = m_compPl;
    crpd.vertex.shader = { m_compVs, u8"VSMain", dr::ShaderStage::Vertex };
    crpd.fragment = dr::FragmentState{}; crpd.fragment->shader = { m_compPs, u8"PSMain", dr::ShaderStage::Fragment };
    crpd.fragment->targets = Span<const dr::ColorTargetState>(&compCt, 1);
    if (m_device->CreateRenderPipeline(crpd, m_compPipe) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    createRenderTargets();
    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void MRTSample::OnRender() {
    using raptor::core::f32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Pass 1: render to 2 RTs.
    enc->TransitionTexture(m_colorRT, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(m_brightRT, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    dr::ColorAttachment ca2[2];
    ca2[0].view = m_colorRTView; ca2[0].loadOp = dr::LoadOp::Clear; ca2[0].storeOp = dr::StoreOp::Store; ca2[0].clearValue = dr::ClearColor(0.1f,0.1f,0.15f,1);
    ca2[1].view = m_brightRTView; ca2[1].loadOp = dr::LoadOp::Clear; ca2[1].storeOp = dr::StoreOp::Store; ca2[1].clearValue = dr::ClearColor::Black();
    dr::RenderPassDesc rpd1{}; rpd1.colorAttachments.Add(ca2[0]); rpd1.colorAttachments.Add(ca2[1]);
    auto* rp1 = enc->BeginRenderPass(rpd1);
    rp1->SetPipeline(m_gbPipe);
    rp1->SetViewport(0,0,static_cast<f32>(m_width),static_cast<f32>(m_height),0,1);
    rp1->SetScissor(0,0,m_width,m_height);
    rp1->SetVertexBuffer(0, m_vb, 0); rp1->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp1->DrawIndexed(6); rp1->End();
    enc->TransitionTexture(m_colorRT, dr::ResourceState::RenderTarget, dr::ResourceState::ShaderRead);
    enc->TransitionTexture(m_brightRT, dr::ResourceState::RenderTarget, dr::ResourceState::ShaderRead);

    // Pass 2: composite to swap chain.
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    dr::ColorAttachment ca1{}; ca1.view = m_swapChain->CurrentTextureView();
    ca1.loadOp = dr::LoadOp::Clear; ca1.storeOp = dr::StoreOp::Store; ca1.clearValue = dr::ClearColor::Black();
    dr::RenderPassDesc rpd2{}; rpd2.colorAttachments.Add(ca1);
    auto* rp2 = enc->BeginRenderPass(rpd2);
    rp2->SetPipeline(m_compPipe); rp2->SetBindGroup(0, m_compBg);
    rp2->SetViewport(0,0,static_cast<f32>(m_width),static_cast<f32>(m_height),0,1);
    rp2->SetScissor(0,0,m_width,m_height);
    rp2->Draw(3); rp2->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue); m_pool->DestroyEncoder(enc);
}

void MRTSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_compPipe) m_device->DestroyRenderPipeline(m_compPipe); if (m_compPl) m_device->DestroyPipelineLayout(m_compPl);
    if (m_compBg) m_device->DestroyBindGroup(m_compBg); if (m_compBgl) m_device->DestroyBindGroupLayout(m_compBgl);
    if (m_gbPipe) m_device->DestroyRenderPipeline(m_gbPipe); if (m_gbPl) m_device->DestroyPipelineLayout(m_gbPl);
    if (m_brightRTView) m_device->DestroyTextureView(m_brightRTView); if (m_brightRT) m_device->DestroyTexture(m_brightRT);
    if (m_colorRTView) m_device->DestroyTextureView(m_colorRTView); if (m_colorRT) m_device->DestroyTexture(m_colorRT);
    if (m_sampler) m_device->DestroySampler(m_sampler);
    if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_compPs) m_device->DestroyShaderModule(m_compPs); if (m_compVs) m_device->DestroyShaderModule(m_compVs);
    if (m_gbPs) m_device->DestroyShaderModule(m_gbPs); if (m_gbVs) m_device->DestroyShaderModule(m_gbVs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { MRTSample app; return app.Run(argc, argv); }
