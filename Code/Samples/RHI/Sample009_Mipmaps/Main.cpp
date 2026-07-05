#include <new>
/// Sample009 — Mipmaps. Ported from Sedulous Sample009_Mipmaps.
/// Textured quad that recedes into the distance showing mip level selection.
/// Uses generateMipmaps() to auto-generate mip chain from a base texture.

#include <cmath>
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
using draconic::core::Matrix4;

class MipmapSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    draconic::core::StringView Title() const override { return u8"Sample009 - Mipmaps"; }
protected:
    draconic::core::Status OnInit() override;
    void OnRender() override;
    void OnResize(draconic::core::u32 w, draconic::core::u32 h) override { m_depthBuf.Recreate(m_device, w, h); }
    void OnShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        Texture2D gTexture : register(t0, space0);
        SamplerState gSampler : register(s0, space0);
        cbuffer UBO : register(b0, space1) { row_major float4x4 MVP; };
        struct VSInput { float3 Position : TEXCOORD0; float2 TexCoord : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = mul(float4(i.Position,1), MVP); o.TexCoord = i.TexCoord; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return gTexture.Sample(gSampler, i.TexCoord); }
    )";
    // Receding floor plane.
    static constexpr float kVerts[] = {
        -4, 0, 0, 0, 0,   4, 0, 0, 8, 0,   4, 0, -20, 8, 10,   -4, 0, -20, 0, 10
    };
    static constexpr draconic::core::u16 kIdx[] = { 0,1,2, 0,2,3 };

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr, *m_ub = nullptr;
    void* m_ubMapped = nullptr;
    dr::Texture* m_tex = nullptr; dr::TextureView* m_texView = nullptr;
    dr::Sampler* m_sampler = nullptr;
    dr::BindGroupLayout *m_texBgl = nullptr, *m_uboBgl = nullptr;
    dr::BindGroup *m_texBg = nullptr, *m_uboBg = nullptr;
    dr::PipelineLayout* m_pl = nullptr; dr::RenderPipeline* m_pipeline = nullptr;
    dr::CommandPool* m_pool = nullptr; dr::Fence* m_fence = nullptr;
    draconic::core::u64 m_fenceVal = 0;
    sf::DepthBuffer m_depthBuf;
};

draconic::core::Status MipmapSample::OnInit() {
    using draconic::core::Status, draconic::core::Span, draconic::core::u8, draconic::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"VS", m_vs) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u8"PSMain", u8"PS", m_ps) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BufferDesc ubd{}; ubd.size = 64; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(ubd, m_ub) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    m_ubMapped = m_ub->Map();

    // Checkerboard base texture 256x256 with 9 mip levels.
    constexpr u32 tw = 256, th = 256, mipCount = 9;
    u8 texPixels[tw * th * 4];
    for (u32 y = 0; y < th; ++y) for (u32 x = 0; x < tw; ++x) {
        bool c = ((x/16) + (y/16)) % 2 == 0;
        u32 i = (y*tw+x)*4;
        texPixels[i]=c?255:30; texPixels[i+1]=c?255:30; texPixels[i+2]=c?255:200; texPixels[i+3]=255;
    }
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = tw; td.height = th;
    td.mipLevelCount = mipCount;
    td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopySrc | dr::TextureUsage::CopyDst | dr::TextureUsage::RenderTarget;
    if (m_device->CreateTexture(td, m_tex) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Upload base mip + generate mips.
    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    dr::TextureDataLayout layout{}; layout.bytesPerRow = tw*4; layout.rowsPerImage = th;
    batch->WriteTexture(m_tex, Span<const u8>(texPixels, sizeof(texPixels)), layout, dr::Extent3D{tw,th,1});
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    // Generate mipmaps then transition to shader-read.
    dr::CommandPool* tmpPool = nullptr; m_device->CreateCommandPool(dr::QueueType::Graphics, tmpPool);
    dr::CommandEncoder* enc = nullptr; tmpPool->CreateEncoder(enc);
    enc->GenerateMipmaps(m_tex);
    // generateMipmaps leaves all mips in TRANSFER_SRC.
    // Transition all mips to SHADER_READ for sampling.
    dr::TextureBarrier tb{}; tb.texture = m_tex;
    tb.oldState = dr::ResourceState::CopySrc; tb.newState = dr::ResourceState::ShaderRead;
    tb.baseMipLevel = 0; tb.mipLevelCount = mipCount;
    tb.baseArrayLayer = 0; tb.arrayLayerCount = 1;
    dr::BarrierGroup bg{}; bg.textureBarriers = Span<const dr::TextureBarrier>(&tb, 1);
    enc->Barrier(bg);
    dr::CommandBuffer* cb = enc->Finish();
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1));
    m_graphicsQueue->WaitIdle();
    tmpPool->DestroyEncoder(enc); m_device->DestroyCommandPool(tmpPool);

    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = mipCount; tvd.arrayLayerCount = 1;
    if (m_device->CreateTextureView(m_tex, tvd, m_texView) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Linear; sd.magFilter = dr::FilterMode::Linear;
    sd.mipmapFilter = dr::MipmapFilterMode::Linear; sd.maxLod = static_cast<draconic::core::f32>(mipCount);
    if (m_device->CreateSampler(sd, m_sampler) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Bind groups: set 0 = texture+sampler, set 1 = UBO.
    dr::BindGroupLayoutEntry tE[2] = { dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
                                        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment) };
    dr::BindGroupLayoutDesc tBgld{}; tBgld.entries = Span<const dr::BindGroupLayoutEntry>(tE, 2);
    if (m_device->CreateBindGroupLayout(tBgld, m_texBgl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BindGroupEntry tBgE[2] = { dr::BindGroupEntry::TextureEntry(m_texView), dr::BindGroupEntry::SamplerEntry(m_sampler) };
    dr::BindGroupDesc tBgd{}; tBgd.layout = m_texBgl; tBgd.entries = Span<const dr::BindGroupEntry>(tBgE, 2);
    if (m_device->CreateBindGroup(tBgd, m_texBg) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    dr::BindGroupLayoutEntry uE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc uBgld{}; uBgld.entries = Span<const dr::BindGroupLayoutEntry>(uE, 1);
    if (m_device->CreateBindGroupLayout(uBgld, m_uboBgl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BindGroupEntry uBgE[1] = { dr::BindGroupEntry::BufferEntry(m_ub, 0, 64) };
    dr::BindGroupDesc uBgd{}; uBgd.layout = m_uboBgl; uBgd.entries = Span<const dr::BindGroupEntry>(uBgE, 1);
    if (m_device->CreateBindGroup(uBgd, m_uboBg) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    dr::BindGroupLayout* sets[2] = { m_texBgl, m_uboBgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    if (m_device->CreatePipelineLayout(pld, m_pl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    m_depthBuf.Recreate(m_device, m_width, m_height);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u8"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u8"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    return draconic::core::ErrorCode::Ok;
}

void MipmapSample::OnRender() {
    using draconic::core::f32, draconic::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != draconic::core::ErrorCode::Ok) return;
    f32 aspect = static_cast<f32>(m_width) / static_cast<f32>(m_height);
    Matrix4 view = Matrix4::LookAtRH(draconic::core::Vector3{0, 2, 2}, draconic::core::Vector3{ 0, 0, -5}, draconic::core::Vector3{0,1,0});
    Matrix4 proj = Matrix4::PerspectiveFovRH(draconic::core::DegreesToRadians(60.0f), aspect, 0.1f, 100.0f);
    Matrix4 mvp = view * proj;
    std::memcpy(m_ubMapped, mvp.Data(), 64);

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != draconic::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(m_depthBuf.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.1f,0.1f,0.15f,1);
    dr::DepthStencilAttachment dsa{}; dsa.view = m_depthBuf.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(m_pipeline); rp->SetBindGroup(0, m_texBg); rp->SetBindGroup(1, m_uboBg);
    rp->SetViewport(0,0,static_cast<f32>(m_width),static_cast<f32>(m_height),0,1);
    rp->SetScissor(0,0,m_width,m_height);
    rp->SetVertexBuffer(0, m_vb, 0); rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(6); rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue); m_pool->DestroyEncoder(enc);
}

void MipmapSample::OnShutdown() {
    m_depthBuf.Destroy(m_device);
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_uboBg) m_device->DestroyBindGroup(m_uboBg); if (m_uboBgl) m_device->DestroyBindGroupLayout(m_uboBgl);
    if (m_texBg) m_device->DestroyBindGroup(m_texBg); if (m_texBgl) m_device->DestroyBindGroupLayout(m_texBgl);
    if (m_sampler) m_device->DestroySampler(m_sampler); if (m_texView) m_device->DestroyTextureView(m_texView);
    if (m_tex) m_device->DestroyTexture(m_tex); if (m_ub) m_device->DestroyBuffer(m_ub);
    if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { MipmapSample app; return app.Run(argc, argv); }
