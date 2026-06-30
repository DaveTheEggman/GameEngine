#include <new>
/// Sample013 — Border Sampler. Ported from Sedulous Sample013_BorderSampler.
/// Demonstrates sampler border colors: TransparentBlack, OpaqueBlack, OpaqueWhite.
/// Three quads with UVs extending beyond [0,1] to show the border region.

#include <cstdint>
#include <cstring>

import draconic.core;
import draconic.rhi;
import draconic.shaders;
import draconic.samples.framework;
import draconic.rhi.vk;

namespace sf = draconic::samples::framework;
namespace dr = draconic::rhi;
namespace ds = draconic::shaders;

class BorderSamplerSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    draconic::core::StringView Title() const override { return u8"Sample013 - Border Sampler"; }
protected:
    draconic::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        Texture2D gTexture : register(t0, space0);
        SamplerState gSampler : register(s0, space0);
        cbuffer UBO : register(b0, space1) { float4 QuadOffset; };
        struct VSInput { float3 Position : TEXCOORD0; float2 TexCoord : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
        PSInput VSMain(VSInput input) {
            PSInput output;
            output.Position = float4(input.Position.xy + QuadOffset.xy, input.Position.z, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET {
            return gTexture.Sample(gSampler, input.TexCoord);
        }
    )";

    // Quad with UVs from -0.5 to 1.5 to show border region.
    static constexpr float kQuadVerts[] = {
        -0.25f, -0.25f, 0.0f,  -0.5f, -0.5f,
         0.25f, -0.25f, 0.0f,   1.5f, -0.5f,
         0.25f,  0.25f, 0.0f,   1.5f,  1.5f,
        -0.25f,  0.25f, 0.0f,  -0.5f,  1.5f,
    };
    static constexpr draconic::core::u16 kQuadIdx[] = { 0, 1, 2, 0, 2, 3 };

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr, *m_ub = nullptr;
    void* m_ubMapped = nullptr;
    dr::Texture* m_tex = nullptr; dr::TextureView* m_texView = nullptr;
    dr::Sampler *m_sampTransparent = nullptr, *m_sampOpaqueBlack = nullptr, *m_sampOpaqueWhite = nullptr;
    dr::BindGroupLayout *m_texBgl = nullptr, *m_uboBgl = nullptr;
    dr::BindGroup *m_bgTransparent = nullptr, *m_bgOpaqueBlack = nullptr, *m_bgOpaqueWhite = nullptr;
    dr::BindGroup *m_uboBg = nullptr;
    dr::PipelineLayout *m_pl = nullptr; dr::RenderPipeline *m_pipeline = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    draconic::core::u64 m_fenceVal = 0;
};

draconic::core::Status BorderSamplerSample::OnInit() {
    using draconic::core::Status, draconic::core::Span, draconic::core::u8, draconic::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"VS", m_vs) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u8"PSMain", u8"PS", m_ps) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kQuadVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kQuadIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Uniform buffer: 3 slots * 256 bytes (DX12 CBV alignment).
    dr::BufferDesc ubd{}; ubd.size = 768; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(ubd, m_ub) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    m_ubMapped = m_ub->Map();
    // Write all 3 offsets upfront.
    float off0[4] = { -0.55f, 0.0f, 0.0f, 0.0f };
    float off1[4] = {  0.0f,  0.0f, 0.0f, 0.0f };
    float off2[4] = {  0.55f, 0.0f, 0.0f, 0.0f };
    std::memcpy(static_cast<u8*>(m_ubMapped),       off0, 16);
    std::memcpy(static_cast<u8*>(m_ubMapped) + 256, off1, 16);
    std::memcpy(static_cast<u8*>(m_ubMapped) + 512, off2, 16);

    // 8x8 checkerboard texture (red/white).
    constexpr u32 tw = 8, th = 8;
    u8 texData[tw * th * 4];
    for (u32 y = 0; y < th; ++y) for (u32 x = 0; x < tw; ++x) {
        u32 i = (y * tw + x) * 4;
        bool white = ((x + y) % 2) == 0;
        texData[i+0] = white ? 255 : 220; texData[i+1] = white ? 255 : 60;
        texData[i+2] = white ? 255 : 60;  texData[i+3] = 255;
    }
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = tw; td.height = th;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst;
    if (m_device->CreateTexture(td, m_tex) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kQuadVerts), sizeof(kQuadVerts)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kQuadIdx), sizeof(kQuadIdx)));
    dr::TextureDataLayout layout{}; layout.bytesPerRow = tw * 4; layout.rowsPerImage = th;
    batch->WriteTexture(m_tex, Span<const u8>(texData, sizeof(texData)), layout, dr::Extent3D{tw, th, 1});
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (m_device->CreateTextureView(m_tex, tvd, m_texView) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Three samplers with ClampToBorder and different border colors.
    auto makeSampler = [&](dr::SamplerBorderColor bc, dr::Sampler*& out) -> draconic::core::Status {
        dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Nearest; sd.magFilter = dr::FilterMode::Nearest;
        sd.addressU = dr::AddressMode::ClampToBorder; sd.addressV = dr::AddressMode::ClampToBorder;
        sd.addressW = dr::AddressMode::ClampToBorder; sd.borderColor = bc;
        return m_device->CreateSampler(sd, out);
    };
    if (makeSampler(dr::SamplerBorderColor::TransparentBlack, m_sampTransparent) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (makeSampler(dr::SamplerBorderColor::OpaqueBlack,      m_sampOpaqueBlack) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (makeSampler(dr::SamplerBorderColor::OpaqueWhite,      m_sampOpaqueWhite) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Bind group layout: set 0 = texture + sampler.
    dr::BindGroupLayoutEntry tE[2] = { dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
                                        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment) };
    dr::BindGroupLayoutDesc tBgld{}; tBgld.entries = Span<const dr::BindGroupLayoutEntry>(tE, 2);
    if (m_device->CreateBindGroupLayout(tBgld, m_texBgl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Three bind groups, one per sampler.
    auto makeBG = [&](dr::Sampler* s, dr::BindGroup*& out) -> draconic::core::Status {
        dr::BindGroupEntry e[2] = { dr::BindGroupEntry::TextureEntry(m_texView), dr::BindGroupEntry::SamplerEntry(s) };
        dr::BindGroupDesc bgd{}; bgd.layout = m_texBgl; bgd.entries = Span<const dr::BindGroupEntry>(e, 2);
        return m_device->CreateBindGroup(bgd, out);
    };
    if (makeBG(m_sampTransparent, m_bgTransparent) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (makeBG(m_sampOpaqueBlack, m_bgOpaqueBlack) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (makeBG(m_sampOpaqueWhite, m_bgOpaqueWhite) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Bind group layout: set 1 = uniform buffer with dynamic offset.
    dr::BindGroupLayoutEntry uEntry = dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex);
    uEntry.hasDynamicOffset = true;
    dr::BindGroupLayoutEntry uE[1] = { uEntry };
    dr::BindGroupLayoutDesc uBgld{}; uBgld.entries = Span<const dr::BindGroupLayoutEntry>(uE, 1);
    if (m_device->CreateBindGroupLayout(uBgld, m_uboBgl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BindGroupEntry uBgE[1] = { dr::BindGroupEntry::BufferEntry(m_ub, 0, 16) };
    dr::BindGroupDesc uBgd{}; uBgd.layout = m_uboBgl; uBgd.entries = Span<const dr::BindGroupEntry>(uBgE, 1);
    if (m_device->CreateBindGroup(uBgd, m_uboBg) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Pipeline layout.
    dr::BindGroupLayout* sets[2] = { m_texBgl, m_uboBgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    if (m_device->CreatePipelineLayout(pld, m_pl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    ct.blend = dr::BlendState::AlphaBlend();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u8"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u8"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    return draconic::core::ErrorCode::Ok;
}

void BorderSamplerSample::OnRender() {
    using draconic::core::f32, draconic::core::u32, draconic::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != draconic::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != draconic::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetPipeline(m_pipeline);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    rp->SetVertexBuffer(0, m_vb, 0);
    rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);

    // Draw 3 quads side by side with different samplers and dynamic UBO offsets.
    dr::BindGroup* texBGs[3] = { m_bgTransparent, m_bgOpaqueBlack, m_bgOpaqueWhite };
    u32 dynOffsets[3] = { 0, 256, 512 };
    for (int i = 0; i < 3; ++i) {
        rp->SetBindGroup(0, texBGs[i]);
        u32 off[1] = { dynOffsets[i] };
        rp->SetBindGroup(1, m_uboBg, Span<const u32>(off, 1));
        rp->DrawIndexed(6);
    }

    rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void BorderSamplerSample::OnShutdown() {
    if (m_ub && m_ubMapped) m_ub->Unmap();
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_uboBg) m_device->DestroyBindGroup(m_uboBg); if (m_uboBgl) m_device->DestroyBindGroupLayout(m_uboBgl);
    if (m_bgOpaqueWhite) m_device->DestroyBindGroup(m_bgOpaqueWhite);
    if (m_bgOpaqueBlack) m_device->DestroyBindGroup(m_bgOpaqueBlack);
    if (m_bgTransparent) m_device->DestroyBindGroup(m_bgTransparent);
    if (m_texBgl) m_device->DestroyBindGroupLayout(m_texBgl);
    if (m_sampOpaqueWhite) m_device->DestroySampler(m_sampOpaqueWhite);
    if (m_sampOpaqueBlack) m_device->DestroySampler(m_sampOpaqueBlack);
    if (m_sampTransparent) m_device->DestroySampler(m_sampTransparent);
    if (m_texView) m_device->DestroyTextureView(m_texView); if (m_tex) m_device->DestroyTexture(m_tex);
    if (m_ub) m_device->DestroyBuffer(m_ub); if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { BorderSamplerSample app; return app.Run(argc, argv); }
