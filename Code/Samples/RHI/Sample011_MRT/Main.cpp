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
    raptor::core::StringView title() const override { return u"Sample011 - MRT"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { createRenderTargets(); }
    void onShutdown() override;
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

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *gbVs_=nullptr, *gbPs_=nullptr, *compVs_=nullptr, *compPs_=nullptr;
    dr::Buffer *vb_=nullptr, *ib_=nullptr;
    dr::Sampler* sampler_ = nullptr;
    dr::PipelineLayout *gbPl_=nullptr, *compPl_=nullptr;
    dr::RenderPipeline *gbPipe_=nullptr, *compPipe_=nullptr;
    dr::BindGroupLayout* compBgl_ = nullptr;
    dr::BindGroup* compBg_ = nullptr;
    dr::Texture *colorRT_=nullptr, *brightRT_=nullptr;
    dr::TextureView *colorRTView_=nullptr, *brightRTView_=nullptr;
    dr::CommandPool* pool_=nullptr; dr::Fence* fence_=nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

void MRTSample::createRenderTargets() {
    if (compBg_) { device_->DestroyBindGroup(compBg_); compBg_ = nullptr; }
    if (colorRTView_) { device_->DestroyTextureView(colorRTView_); colorRTView_ = nullptr; }
    if (colorRT_) { device_->DestroyTexture(colorRT_); colorRT_ = nullptr; }
    if (brightRTView_) { device_->DestroyTextureView(brightRTView_); brightRTView_ = nullptr; }
    if (brightRT_) { device_->DestroyTexture(brightRT_); brightRT_ = nullptr; }

    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = width_; td.height = height_;
    td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::Sampled;
    device_->CreateTexture(td, colorRT_);
    device_->CreateTexture(td, brightRT_);
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    device_->CreateTextureView(colorRT_, tvd, colorRTView_);
    device_->CreateTextureView(brightRT_, tvd, brightRTView_);

    dr::BindGroupEntry bgE[3] = { dr::BindGroupEntry::TextureEntry(colorRTView_),
                                   dr::BindGroupEntry::TextureEntry(brightRTView_),
                                   dr::BindGroupEntry::SamplerEntry(sampler_) };
    dr::BindGroupDesc bgd{}; bgd.layout = compBgl_; bgd.entries = raptor::core::Span<const dr::BindGroupEntry>(bgE, 3);
    device_->CreateBindGroup(bgd, compBg_);
}

raptor::core::Status MRTSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kGBufShader, ds::ShaderStage::Vertex,   u"VSMain", u"GBufVS", gbVs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kGBufShader, ds::ShaderStage::Fragment, u"PSMain", u"GBufPS", gbPs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kCompShader, ds::ShaderStage::Vertex,   u"VSMain", u"CompVS", compVs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kCompShader, ds::ShaderStage::Fragment, u"PSMain", u"CompPS", compPs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Nearest; sd.magFilter = dr::FilterMode::Nearest;
    sd.addressU = dr::AddressMode::ClampToEdge; sd.addressV = dr::AddressMode::ClampToEdge;
    if (device_->CreateSampler(sd, sampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // GBuffer pipeline (empty layout, 2 color targets).
    dr::PipelineLayoutDesc gpld{}; if (device_->CreatePipelineLayout(gpld, gbPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState gbCt[2] = { {dr::TextureFormat::RGBA8Unorm}, {dr::TextureFormat::RGBA8Unorm} };
    dr::RenderPipelineDesc grpd{}; grpd.layout = gbPl_;
    grpd.vertex.shader = { gbVs_, u"VSMain", dr::ShaderStage::Vertex };
    grpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    grpd.fragment = dr::FragmentState{}; grpd.fragment->shader = { gbPs_, u"PSMain", dr::ShaderStage::Fragment };
    grpd.fragment->targets = Span<const dr::ColorTargetState>(gbCt, 2);
    if (device_->CreateRenderPipeline(grpd, gbPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Composite BGL + pipeline (3 bindings: 2 textures + 1 sampler).
    dr::BindGroupLayoutEntry cE[3] = {
        dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::SampledTexture(1, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment),
    };
    dr::BindGroupLayoutDesc cBgld{}; cBgld.entries = Span<const dr::BindGroupLayoutEntry>(cE, 3);
    if (device_->CreateBindGroupLayout(cBgld, compBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* cSets[1] = { compBgl_ };
    dr::PipelineLayoutDesc cpld{}; cpld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(cSets, 1);
    if (device_->CreatePipelineLayout(cpld, compPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::ColorTargetState compCt{}; compCt.format = swapChain_->Format();
    dr::RenderPipelineDesc crpd{}; crpd.layout = compPl_;
    crpd.vertex.shader = { compVs_, u"VSMain", dr::ShaderStage::Vertex };
    crpd.fragment = dr::FragmentState{}; crpd.fragment->shader = { compPs_, u"PSMain", dr::ShaderStage::Fragment };
    crpd.fragment->targets = Span<const dr::ColorTargetState>(&compCt, 1);
    if (device_->CreateRenderPipeline(crpd, compPipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    createRenderTargets();
    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void MRTSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Pass 1: render to 2 RTs.
    enc->TransitionTexture(colorRT_, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(brightRT_, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    dr::ColorAttachment ca2[2];
    ca2[0].view = colorRTView_; ca2[0].loadOp = dr::LoadOp::Clear; ca2[0].storeOp = dr::StoreOp::Store; ca2[0].clearValue = dr::ClearColor(0.1f,0.1f,0.15f,1);
    ca2[1].view = brightRTView_; ca2[1].loadOp = dr::LoadOp::Clear; ca2[1].storeOp = dr::StoreOp::Store; ca2[1].clearValue = dr::ClearColor::Black();
    dr::RenderPassDesc rpd1{}; rpd1.colorAttachments.Add(ca2[0]); rpd1.colorAttachments.Add(ca2[1]);
    auto* rp1 = enc->BeginRenderPass(rpd1);
    rp1->SetPipeline(gbPipe_);
    rp1->SetViewport(0,0,static_cast<f32>(width_),static_cast<f32>(height_),0,1);
    rp1->SetScissor(0,0,width_,height_);
    rp1->SetVertexBuffer(0, vb_, 0); rp1->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp1->DrawIndexed(6); rp1->End();
    enc->TransitionTexture(colorRT_, dr::ResourceState::RenderTarget, dr::ResourceState::ShaderRead);
    enc->TransitionTexture(brightRT_, dr::ResourceState::RenderTarget, dr::ResourceState::ShaderRead);

    // Pass 2: composite to swap chain.
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    dr::ColorAttachment ca1{}; ca1.view = swapChain_->CurrentTextureView();
    ca1.loadOp = dr::LoadOp::Clear; ca1.storeOp = dr::StoreOp::Store; ca1.clearValue = dr::ClearColor::Black();
    dr::RenderPassDesc rpd2{}; rpd2.colorAttachments.Add(ca1);
    auto* rp2 = enc->BeginRenderPass(rpd2);
    rp2->SetPipeline(compPipe_); rp2->SetBindGroup(0, compBg_);
    rp2->SetViewport(0,0,static_cast<f32>(width_),static_cast<f32>(height_),0,1);
    rp2->SetScissor(0,0,width_,height_);
    rp2->Draw(3); rp2->End();
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_); pool_->DestroyEncoder(enc);
}

void MRTSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (compPipe_) device_->DestroyRenderPipeline(compPipe_); if (compPl_) device_->DestroyPipelineLayout(compPl_);
    if (compBg_) device_->DestroyBindGroup(compBg_); if (compBgl_) device_->DestroyBindGroupLayout(compBgl_);
    if (gbPipe_) device_->DestroyRenderPipeline(gbPipe_); if (gbPl_) device_->DestroyPipelineLayout(gbPl_);
    if (brightRTView_) device_->DestroyTextureView(brightRTView_); if (brightRT_) device_->DestroyTexture(brightRT_);
    if (colorRTView_) device_->DestroyTextureView(colorRTView_); if (colorRT_) device_->DestroyTexture(colorRT_);
    if (sampler_) device_->DestroySampler(sampler_);
    if (ib_) device_->DestroyBuffer(ib_); if (vb_) device_->DestroyBuffer(vb_);
    if (compPs_) device_->DestroyShaderModule(compPs_); if (compVs_) device_->DestroyShaderModule(compVs_);
    if (gbPs_) device_->DestroyShaderModule(gbPs_); if (gbVs_) device_->DestroyShaderModule(gbVs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { MRTSample app; return app.run(argc, argv); }
