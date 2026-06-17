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
    raptor::core::StringView title() const override { return u"Sample002 - Textured Quad"; }

protected:
    raptor::core::Status onInit() override;
    void          onRender() override;
    void          onShutdown() override;

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

    ds::Compiler*        compiler_  = nullptr;
    dr::Buffer*          vb_        = nullptr;
    dr::Buffer*          ib_        = nullptr;
    dr::ShaderModule*    vs_        = nullptr;
    dr::ShaderModule*    ps_        = nullptr;
    dr::Texture*         tex_       = nullptr;
    dr::TextureView*     texView_   = nullptr;
    dr::Sampler*         sampler_   = nullptr;
    dr::BindGroupLayout* bgl_       = nullptr;
    dr::BindGroup*       bg_        = nullptr;
    dr::PipelineLayout*  pl_        = nullptr;
    dr::RenderPipeline*  pipeline_  = nullptr;
    dr::CommandPool*     pool_      = nullptr;
    dr::Fence*           fence_     = nullptr;
    raptor::core::u64           fenceVal_  = 0;
};

raptor::core::Status TextureSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShaderSource, ds::ShaderStage::Vertex,   u"VSMain", u"QuadVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShaderSource, ds::ShaderStage::Fragment, u"PSMain", u"QuadPS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex + index buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kVertexData); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIndexData); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

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
    if (device_->CreateTexture(td, tex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Upload.
    dr::TransferBatch* batch = nullptr;
    graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVertexData), sizeof(kVertexData)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIndexData), sizeof(kIndexData)));
    dr::TextureDataLayout layout{}; layout.bytesPerRow = tw * 4; layout.rowsPerImage = th;
    batch->WriteTexture(tex_, Span<const u8>(texPixels, sizeof(texPixels)), layout, dr::Extent3D{tw, th, 1});
    batch->Submit();
    graphicsQueue_->DestroyTransferBatch(batch);

    // Texture view + sampler.
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (device_->CreateTextureView(tex_, tvd, texView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Nearest; sd.magFilter = dr::FilterMode::Nearest;
    if (device_->CreateSampler(sd, sampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind group layout + bind group.
    dr::BindGroupLayoutEntry bglEntries[2] = {
        dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment),
    };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglEntries, 2);
    if (device_->CreateBindGroupLayout(bgld, bgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry bgEntries[2] = {
        dr::BindGroupEntry::TextureEntry(texView_),
        dr::BindGroupEntry::SamplerEntry(sampler_),
    };
    dr::BindGroupDesc bgd{}; bgd.layout = bgl_; bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 2);
    if (device_->CreateBindGroup(bgd, bg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout.
    dr::BindGroupLayout* sets[1] = { bgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline.
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format(); ct.writeMask = dr::ColorWriteMask::All;

    dr::RenderPipelineDesc rpd{};
    rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    return raptor::core::ErrorCode::Ok;
}

void TextureSample::onRender() {
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
    rp->SetBindGroup(0, bg_);
    rp->SetViewport(0, 0, static_cast<raptor::core::f32>(width_), static_cast<raptor::core::f32>(height_), 0, 1);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(6);
    rp->End();

    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish();
    fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(raptor::core::Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void TextureSample::onShutdown() {
    if (fence_)    device_->DestroyFence(fence_);
    if (pool_)     device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_);
    if (pl_)       device_->DestroyPipelineLayout(pl_);
    if (bg_)       device_->DestroyBindGroup(bg_);
    if (bgl_)      device_->DestroyBindGroupLayout(bgl_);
    if (sampler_)  device_->DestroySampler(sampler_);
    if (texView_)  device_->DestroyTextureView(texView_);
    if (tex_)      device_->DestroyTexture(tex_);
    if (ps_)       device_->DestroyShaderModule(ps_);
    if (vs_)       device_->DestroyShaderModule(vs_);
    if (ib_)       device_->DestroyBuffer(ib_);
    if (vb_)       device_->DestroyBuffer(vb_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { TextureSample app; return app.run(argc, argv); }
