#include <new>
/// Sample002 — Textured Quad. Ported from Sedulous Sample002_Textures.
/// Renders a checkerboard-textured quad using texture, sampler, bind group.

#include <cstdint>
#include <cstdio>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class TextureSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView Title() const override { return u"Sample002 - Textured Quad"; }

protected:
    raptor::core::Status OnInit() override;
    void          OnRender() override;
    void          OnShutdown() override;

private:
    static constexpr const char8_t kShaderSource[] = u8R"(
        Texture2D gTexture : register(t0, space0);
        SamplerState gSampler : register(s0, space0);
        struct VSInput { float3 Position : TEXCOORD0; float2 TexCoord : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
        PSInput VSMain(VSInput input) {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET {
            return gTexture.Sample(gSampler, input.TexCoord);
        }
    )";

    static constexpr float kVertexData[] = {
        -0.5f,  0.5f, 0.0f,   0.0f, 0.0f,
         0.5f,  0.5f, 0.0f,   1.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f,   0.0f, 1.0f,
    };
    static constexpr raptor::core::u16 kIndexData[] = { 0, 1, 2, 0, 2, 3 };

    ds::Compiler*        m_compiler  = nullptr;
    dr::Buffer*          m_vb        = nullptr;
    dr::Buffer*          m_ib        = nullptr;
    dr::ShaderModule*    m_vs        = nullptr;
    dr::ShaderModule*    m_ps        = nullptr;
    dr::Texture*         m_tex       = nullptr;
    dr::TextureView*     m_texView   = nullptr;
    dr::Sampler*         m_sampler   = nullptr;
    dr::BindGroupLayout* m_bgl       = nullptr;
    dr::BindGroup*       m_bg        = nullptr;
    dr::PipelineLayout*  m_pl        = nullptr;
    dr::RenderPipeline*  m_pipeline  = nullptr;
    dr::CommandPool*     m_pool      = nullptr;
    dr::Fence*           m_fence     = nullptr;
    raptor::core::u64           m_fenceVal  = 0;
};

raptor::core::Status TextureSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShaderSource, ds::ShaderStage::Vertex,   u"VSMain", u"QuadVS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShaderSource, ds::ShaderStage::Fragment, u"PSMain", u"QuadPS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex + index buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kVertexData); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIndexData); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Checkerboard texture 64x64 RGBA8.
    constexpr u32 tw = 64, th = 64;
    u8 texPixels[tw * th * 4];
    for (u32 y = 0; y < th; ++y)
        for (u32 x = 0; x < tw; ++x) {
            bool checker = ((x / 8) + (y / 8)) % 2 == 0;
            u32 i = (y * tw + x) * 4;
            texPixels[i] = checker ? 255 : 50; texPixels[i+1] = checker ? 255 : 50;
            texPixels[i+2] = checker ? 255 : 200; texPixels[i+3] = 255;
        }

    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = tw; td.height = th;
    td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst;
    if (m_device->CreateTexture(td, m_tex) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Upload.
    dr::TransferBatch* batch = nullptr;
    m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kVertexData), sizeof(kVertexData)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kIndexData), sizeof(kIndexData)));
    dr::TextureDataLayout layout{}; layout.bytesPerRow = tw * 4; layout.rowsPerImage = th;
    batch->WriteTexture(m_tex, Span<const u8>(texPixels, sizeof(texPixels)), layout, dr::Extent3D{tw, th, 1});
    batch->Submit();
    m_graphicsQueue->DestroyTransferBatch(batch);

    // Texture view + sampler.
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (m_device->CreateTextureView(m_tex, tvd, m_texView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Nearest; sd.magFilter = dr::FilterMode::Nearest;
    if (m_device->CreateSampler(sd, m_sampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind group layout + bind group.
    dr::BindGroupLayoutEntry bglEntries[2] = {
        dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment),
    };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglEntries, 2);
    if (m_device->CreateBindGroupLayout(bgld, m_bgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry bgEntries[2] = {
        dr::BindGroupEntry::TextureEntry(m_texView),
        dr::BindGroupEntry::SamplerEntry(m_sampler),
    };
    dr::BindGroupDesc bgd{}; bgd.layout = m_bgl; bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 2);
    if (m_device->CreateBindGroup(bgd, m_bg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout.
    dr::BindGroupLayout* sets[1] = { m_bgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline.
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format(); ct.writeMask = dr::ColorWriteMask::All;

    dr::RenderPipelineDesc rpd{};
    rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    return raptor::core::ErrorCode::Ok;
}

void TextureSample::OnRender() {
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();

    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);

    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(m_pipeline);
    rp->SetBindGroup(0, m_bg);
    rp->SetViewport(0, 0, static_cast<raptor::core::f32>(m_width), static_cast<raptor::core::f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    rp->SetVertexBuffer(0, m_vb, 0);
    rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(6);
    rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish();
    m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(raptor::core::Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void TextureSample::OnShutdown() {
    if (m_fence)    m_device->DestroyFence(m_fence);
    if (m_pool)     m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline);
    if (m_pl)       m_device->DestroyPipelineLayout(m_pl);
    if (m_bg)       m_device->DestroyBindGroup(m_bg);
    if (m_bgl)      m_device->DestroyBindGroupLayout(m_bgl);
    if (m_sampler)  m_device->DestroySampler(m_sampler);
    if (m_texView)  m_device->DestroyTextureView(m_texView);
    if (m_tex)      m_device->DestroyTexture(m_tex);
    if (m_ps)       m_device->DestroyShaderModule(m_ps);
    if (m_vs)       m_device->DestroyShaderModule(m_vs);
    if (m_ib)       m_device->DestroyBuffer(m_ib);
    if (m_vb)       m_device->DestroyBuffer(m_vb);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { TextureSample app; return app.Run(argc, argv); }
