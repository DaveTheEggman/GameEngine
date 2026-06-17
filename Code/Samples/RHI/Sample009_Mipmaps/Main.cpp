#include <new>
/// Sample009 — Mipmaps. Ported from Sedulous Sample009_Mipmaps.
/// Textured quad that recedes into the distance showing mip level selection.
/// Uses generateMipmaps() to auto-generate mip chain from a base texture.

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
using raptor::core::Mat4;

class MipmapSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample009 - Mipmaps"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { depthBuf_.recreate(device_, w, h); }
    void onShutdown() override;
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
    static constexpr raptor::core::u16 kIdx[] = { 0,1,2, 0,2,3 };

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *ub_ = nullptr;
    void* ubMapped_ = nullptr;
    dr::Texture* tex_ = nullptr; dr::TextureView* texView_ = nullptr;
    dr::Sampler* sampler_ = nullptr;
    dr::BindGroupLayout *texBgl_ = nullptr, *uboBgl_ = nullptr;
    dr::BindGroup *texBg_ = nullptr, *uboBg_ = nullptr;
    dr::PipelineLayout* pl_ = nullptr; dr::RenderPipeline* pipeline_ = nullptr;
    dr::CommandPool* pool_ = nullptr; dr::Fence* fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    sf::DepthBuffer depthBuf_;
};

raptor::core::Status MipmapSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ubd{}; ubd.size = 64; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(ubd, ub_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    ubMapped_ = ub_->Map();

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
    if (device_->CreateTexture(td, tex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Upload base mip + generate mips.
    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    dr::TextureDataLayout layout{}; layout.bytesPerRow = tw*4; layout.rowsPerImage = th;
    batch->WriteTexture(tex_, Span<const u8>(texPixels, sizeof(texPixels)), layout, dr::Extent3D{tw,th,1});
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    // Generate mipmaps then transition to shader-read.
    dr::CommandPool* tmpPool = nullptr; device_->CreateCommandPool(dr::QueueType::Graphics, tmpPool);
    dr::CommandEncoder* enc = nullptr; tmpPool->CreateEncoder(enc);
    enc->GenerateMipmaps(tex_);
    // generateMipmaps leaves all mips in TRANSFER_SRC.
    // Transition all mips to SHADER_READ for sampling.
    dr::TextureBarrier tb{}; tb.texture = tex_;
    tb.oldState = dr::ResourceState::CopySrc; tb.newState = dr::ResourceState::ShaderRead;
    tb.baseMipLevel = 0; tb.mipLevelCount = mipCount;
    tb.baseArrayLayer = 0; tb.arrayLayerCount = 1;
    dr::BarrierGroup bg{}; bg.textureBarriers = Span<const dr::TextureBarrier>(&tb, 1);
    enc->Barrier(bg);
    dr::CommandBuffer* cb = enc->Finish();
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1));
    graphicsQueue_->WaitIdle();
    tmpPool->DestroyEncoder(enc); device_->DestroyCommandPool(tmpPool);

    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = mipCount; tvd.arrayLayerCount = 1;
    if (device_->CreateTextureView(tex_, tvd, texView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Linear; sd.magFilter = dr::FilterMode::Linear;
    sd.mipmapFilter = dr::MipmapFilterMode::Linear; sd.maxLod = static_cast<raptor::core::f32>(mipCount);
    if (device_->CreateSampler(sd, sampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind groups: set 0 = texture+sampler, set 1 = UBO.
    dr::BindGroupLayoutEntry tE[2] = { dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
                                        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment) };
    dr::BindGroupLayoutDesc tBgld{}; tBgld.entries = Span<const dr::BindGroupLayoutEntry>(tE, 2);
    if (device_->CreateBindGroupLayout(tBgld, texBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry tBgE[2] = { dr::BindGroupEntry::TextureEntry(texView_), dr::BindGroupEntry::SamplerEntry(sampler_) };
    dr::BindGroupDesc tBgd{}; tBgd.layout = texBgl_; tBgd.entries = Span<const dr::BindGroupEntry>(tBgE, 2);
    if (device_->CreateBindGroup(tBgd, texBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupLayoutEntry uE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc uBgld{}; uBgld.entries = Span<const dr::BindGroupLayoutEntry>(uE, 1);
    if (device_->CreateBindGroupLayout(uBgld, uboBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry uBgE[1] = { dr::BindGroupEntry::BufferEntry(ub_, 0, 64) };
    dr::BindGroupDesc uBgd{}; uBgd.layout = uboBgl_; uBgd.entries = Span<const dr::BindGroupEntry>(uBgE, 1);
    if (device_->CreateBindGroup(uBgd, uboBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupLayout* sets[2] = { texBgl_, uboBgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    depthBuf_.recreate(device_, width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void MipmapSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);
    Mat4 view = Mat4::LookAtRH(raptor::core::Vec3{0, 2, 2}, raptor::core::Vec3{ 0, 0, -5}, raptor::core::Vec3{0,1,0});
    Mat4 proj = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(60.0f), aspect, 0.1f, 100.0f);
    Mat4 mvp = view * proj;
    std::memcpy(ubMapped_, mvp.Data(), 64);

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(depthBuf_.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.1f,0.1f,0.15f,1);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthBuf_.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(pipeline_); rp->SetBindGroup(0, texBg_); rp->SetBindGroup(1, uboBg_);
    rp->SetViewport(0,0,static_cast<f32>(width_),static_cast<f32>(height_),0,1);
    rp->SetScissor(0,0,width_,height_);
    rp->SetVertexBuffer(0, vb_, 0); rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(6); rp->End();
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_); pool_->DestroyEncoder(enc);
}

void MipmapSample::onShutdown() {
    depthBuf_.destroy(device_);
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_); if (pl_) device_->DestroyPipelineLayout(pl_);
    if (uboBg_) device_->DestroyBindGroup(uboBg_); if (uboBgl_) device_->DestroyBindGroupLayout(uboBgl_);
    if (texBg_) device_->DestroyBindGroup(texBg_); if (texBgl_) device_->DestroyBindGroupLayout(texBgl_);
    if (sampler_) device_->DestroySampler(sampler_); if (texView_) device_->DestroyTextureView(texView_);
    if (tex_) device_->DestroyTexture(tex_); if (ub_) device_->DestroyBuffer(ub_);
    if (ib_) device_->DestroyBuffer(ib_); if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { MipmapSample app; return app.run(argc, argv); }
