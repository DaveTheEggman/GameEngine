#include <new>
/// Sample013 — Border Sampler. Ported from Sedulous Sample013_BorderSampler.
/// Demonstrates sampler border colors: TransparentBlack, OpaqueBlack, OpaqueWhite.
/// Three quads with UVs extending beyond [0,1] to show the border region.

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

class BorderSamplerSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample013 - Border Sampler"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
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
    static constexpr raptor::core::u16 kQuadIdx[] = { 0, 1, 2, 0, 2, 3 };

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *ub_ = nullptr;
    void* ubMapped_ = nullptr;
    dr::Texture* tex_ = nullptr; dr::TextureView* texView_ = nullptr;
    dr::Sampler *sampTransparent_ = nullptr, *sampOpaqueBlack_ = nullptr, *sampOpaqueWhite_ = nullptr;
    dr::BindGroupLayout *texBgl_ = nullptr, *uboBgl_ = nullptr;
    dr::BindGroup *bgTransparent_ = nullptr, *bgOpaqueBlack_ = nullptr, *bgOpaqueWhite_ = nullptr;
    dr::BindGroup *uboBg_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr; dr::RenderPipeline *pipeline_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status BorderSamplerSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kQuadVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kQuadIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Uniform buffer: 3 slots * 256 bytes (DX12 CBV alignment).
    dr::BufferDesc ubd{}; ubd.size = 768; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(ubd, ub_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    ubMapped_ = ub_->Map();
    // Write all 3 offsets upfront.
    float off0[4] = { -0.55f, 0.0f, 0.0f, 0.0f };
    float off1[4] = {  0.0f,  0.0f, 0.0f, 0.0f };
    float off2[4] = {  0.55f, 0.0f, 0.0f, 0.0f };
    std::memcpy(static_cast<u8*>(ubMapped_),       off0, 16);
    std::memcpy(static_cast<u8*>(ubMapped_) + 256, off1, 16);
    std::memcpy(static_cast<u8*>(ubMapped_) + 512, off2, 16);

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
    if (device_->CreateTexture(td, tex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kQuadVerts), sizeof(kQuadVerts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kQuadIdx), sizeof(kQuadIdx)));
    dr::TextureDataLayout layout{}; layout.bytesPerRow = tw * 4; layout.rowsPerImage = th;
    batch->WriteTexture(tex_, Span<const u8>(texData, sizeof(texData)), layout, dr::Extent3D{tw, th, 1});
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (device_->CreateTextureView(tex_, tvd, texView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Three samplers with ClampToBorder and different border colors.
    auto makeSampler = [&](dr::SamplerBorderColor bc, dr::Sampler*& out) -> raptor::core::Status {
        dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Nearest; sd.magFilter = dr::FilterMode::Nearest;
        sd.addressU = dr::AddressMode::ClampToBorder; sd.addressV = dr::AddressMode::ClampToBorder;
        sd.addressW = dr::AddressMode::ClampToBorder; sd.borderColor = bc;
        return device_->CreateSampler(sd, out);
    };
    if (makeSampler(dr::SamplerBorderColor::TransparentBlack, sampTransparent_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (makeSampler(dr::SamplerBorderColor::OpaqueBlack,      sampOpaqueBlack_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (makeSampler(dr::SamplerBorderColor::OpaqueWhite,      sampOpaqueWhite_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind group layout: set 0 = texture + sampler.
    dr::BindGroupLayoutEntry tE[2] = { dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
                                        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment) };
    dr::BindGroupLayoutDesc tBgld{}; tBgld.entries = Span<const dr::BindGroupLayoutEntry>(tE, 2);
    if (device_->CreateBindGroupLayout(tBgld, texBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Three bind groups, one per sampler.
    auto makeBG = [&](dr::Sampler* s, dr::BindGroup*& out) -> raptor::core::Status {
        dr::BindGroupEntry e[2] = { dr::BindGroupEntry::TextureEntry(texView_), dr::BindGroupEntry::SamplerEntry(s) };
        dr::BindGroupDesc bgd{}; bgd.layout = texBgl_; bgd.entries = Span<const dr::BindGroupEntry>(e, 2);
        return device_->CreateBindGroup(bgd, out);
    };
    if (makeBG(sampTransparent_, bgTransparent_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (makeBG(sampOpaqueBlack_, bgOpaqueBlack_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (makeBG(sampOpaqueWhite_, bgOpaqueWhite_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind group layout: set 1 = uniform buffer with dynamic offset.
    dr::BindGroupLayoutEntry uEntry = dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex);
    uEntry.hasDynamicOffset = true;
    dr::BindGroupLayoutEntry uE[1] = { uEntry };
    dr::BindGroupLayoutDesc uBgld{}; uBgld.entries = Span<const dr::BindGroupLayoutEntry>(uE, 1);
    if (device_->CreateBindGroupLayout(uBgld, uboBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry uBgE[1] = { dr::BindGroupEntry::BufferEntry(ub_, 0, 16) };
    dr::BindGroupDesc uBgd{}; uBgd.layout = uboBgl_; uBgd.entries = Span<const dr::BindGroupEntry>(uBgE, 1);
    if (device_->CreateBindGroup(uBgd, uboBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout.
    dr::BindGroupLayout* sets[2] = { texBgl_, uboBgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    ct.blend = dr::BlendState::AlphaBlend();
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

void BorderSamplerSample::onRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetPipeline(pipeline_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);

    // Draw 3 quads side by side with different samplers and dynamic UBO offsets.
    dr::BindGroup* texBGs[3] = { bgTransparent_, bgOpaqueBlack_, bgOpaqueWhite_ };
    u32 dynOffsets[3] = { 0, 256, 512 };
    for (int i = 0; i < 3; ++i) {
        rp->SetBindGroup(0, texBGs[i]);
        u32 off[1] = { dynOffsets[i] };
        rp->SetBindGroup(1, uboBg_, Span<const u32>(off, 1));
        rp->DrawIndexed(6);
    }

    rp->End();
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void BorderSamplerSample::onShutdown() {
    if (ub_ && ubMapped_) ub_->Unmap();
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_); if (pl_) device_->DestroyPipelineLayout(pl_);
    if (uboBg_) device_->DestroyBindGroup(uboBg_); if (uboBgl_) device_->DestroyBindGroupLayout(uboBgl_);
    if (bgOpaqueWhite_) device_->DestroyBindGroup(bgOpaqueWhite_);
    if (bgOpaqueBlack_) device_->DestroyBindGroup(bgOpaqueBlack_);
    if (bgTransparent_) device_->DestroyBindGroup(bgTransparent_);
    if (texBgl_) device_->DestroyBindGroupLayout(texBgl_);
    if (sampOpaqueWhite_) device_->DestroySampler(sampOpaqueWhite_);
    if (sampOpaqueBlack_) device_->DestroySampler(sampOpaqueBlack_);
    if (sampTransparent_) device_->DestroySampler(sampTransparent_);
    if (texView_) device_->DestroyTextureView(texView_); if (tex_) device_->DestroyTexture(tex_);
    if (ub_) device_->DestroyBuffer(ub_); if (ib_) device_->DestroyBuffer(ib_); if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { BorderSamplerSample app; return app.run(argc, argv); }
