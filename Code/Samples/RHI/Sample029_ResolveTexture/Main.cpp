#include <new>
/// Sample029 -- ResolveTexture (Explicit 4x MSAA). Ported from Sedulous Sample029_ResolveTexture.
/// Demonstrates explicit MSAA resolve via CommandEncoder::resolveTexture().
/// Unlike Sample010 (which uses ColorAttachment.resolveTarget for automatic
/// render-pass resolve), this sample renders to a 4x MSAA target and then
/// manually resolves to the swapchain using the resolveTexture command.

#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class ResolveTextureSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample029 - ResolveTexture (Explicit 4x MSAA)"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateMsaaTarget(w, h); }
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput {
            float3 Position : TEXCOORD0;
            float4 Color    : TEXCOORD1;
        };
        struct PSInput {
            float4 Position : SV_POSITION;
            float4 Color    : COLOR0;
        };
        PSInput VSMain(VSInput input) {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.Color = input.Color;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET {
            return input.Color;
        }
    )";

    // Star shape: great for showing MSAA on diagonal edges.
    // Each vertex: float3 Position, float4 Color (stride = 28).
    static constexpr float kVerts[] = {
        // Center
         0.0f,    0.0f,   0.0f,   1.0f, 1.0f, 1.0f, 1.0f,
        // Outer tips (radius 0.7)
         0.0f,    0.7f,   0.0f,   1.0f, 0.2f, 0.2f, 1.0f,
         0.665f,  0.216f, 0.0f,   0.2f, 1.0f, 0.2f, 1.0f,
         0.411f, -0.566f, 0.0f,   0.2f, 0.3f, 1.0f, 1.0f,
        -0.411f, -0.566f, 0.0f,   1.0f, 1.0f, 0.2f, 1.0f,
        -0.665f,  0.216f, 0.0f,   1.0f, 0.2f, 1.0f, 1.0f,
        // Inner notches (radius 0.25)
         0.238f,  0.327f, 0.0f,   0.8f, 0.7f, 0.5f, 1.0f,
         0.385f, -0.125f, 0.0f,   0.5f, 0.8f, 0.7f, 1.0f,
         0.0f,  -0.405f, 0.0f,   0.5f, 0.5f, 0.9f, 1.0f,
        -0.385f, -0.125f, 0.0f,   0.9f, 0.8f, 0.5f, 1.0f,
        -0.238f,  0.327f, 0.0f,   0.9f, 0.5f, 0.8f, 1.0f,
    };

    static constexpr raptor::core::u16 kIdx[] = {
        0, 1, 6,   0, 6, 2,
        0, 2, 7,   0, 7, 3,
        0, 3, 8,   0, 8, 4,
        0, 4, 9,   0, 9, 5,
        0, 5, 10,  0, 10, 1,
    };

    static constexpr raptor::core::u32 kSamples = 4;

    void recreateMsaaTarget(raptor::core::u32 w, raptor::core::u32 h);

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *pipeline_ = nullptr;
    dr::Texture *msaaTex_ = nullptr;
    dr::TextureView *msaaView_ = nullptr;
    dr::CommandPool *pool_ = nullptr;
    dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

void ResolveTextureSample::recreateMsaaTarget(raptor::core::u32 w, raptor::core::u32 h) {
    if (msaaView_) { device_->DestroyTextureView(msaaView_); msaaView_ = nullptr; }
    if (msaaTex_) { device_->DestroyTexture(msaaTex_); msaaTex_ = nullptr; }
    // Need RenderTarget (to draw into) and CopySrc (source for resolveTexture).
    dr::TextureDesc td{};
    td.format = swapChain_->Format(); td.width = w; td.height = h; td.sampleCount = kSamples;
    td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::CopySrc;
    td.label = u"MsaaRT";
    device_->CreateTexture(td, msaaTex_);
    dr::TextureViewDesc tvd{}; tvd.format = swapChain_->Format(); tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    device_->CreateTextureView(msaaTex_, tvd, msaaView_);
}

raptor::core::Status ResolveTextureSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"ResolveVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"ResolvePS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex and index buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly; vbd.label = u"ResolveVB";
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly; ibd.label = u"ResolveIB";
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    // Pipeline layout (no bind groups).
    dr::PipelineLayoutDesc pld{}; pld.label = u"ResolvePL";
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // MSAA render target.
    recreateMsaaTarget(width_, height_);

    // Render pipeline with multisample count = 4.
    dr::VertexAttribute attrs[2] = {
        { dr::VertexFormat::Float32x3,  0, 0 },
        { dr::VertexFormat::Float32x4, 12, 1 },
    };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();

    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_; rpd.label = u"ResolvePipeline";
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.multisample.count = kSamples;
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void ResolveTextureSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // === Step 1: Render star into MSAA texture ===
    enc->TransitionTexture(msaaTex_, dr::ResourceState::CopySrc, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{};
    ca.view = msaaView_;
    ca.resolveTarget = nullptr;  // No auto-resolve -- we do it manually.
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(pipeline_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(30);
    rp->End();

    // === Step 2: Transition for resolve ===
    // MSAA texture: RenderTarget -> CopySrc
    enc->TransitionTexture(msaaTex_, dr::ResourceState::RenderTarget, dr::ResourceState::CopySrc);
    // Swapchain: Present -> CopyDst
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Present, dr::ResourceState::CopyDst);

    // === Step 3: Explicit resolve MSAA -> swapchain ===
    enc->ResolveTexture(msaaTex_, swapChain_->CurrentTexture());

    // === Step 4: Transition swapchain back to Present ===
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::CopyDst, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_); pool_->DestroyEncoder(enc);
}

void ResolveTextureSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_);
    if (pl_) device_->DestroyPipelineLayout(pl_);
    if (msaaView_) device_->DestroyTextureView(msaaView_);
    if (msaaTex_) device_->DestroyTexture(msaaTex_);
    if (ps_) device_->DestroyShaderModule(ps_);
    if (vs_) device_->DestroyShaderModule(vs_);
    if (ib_) device_->DestroyBuffer(ib_);
    if (vb_) device_->DestroyBuffer(vb_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { ResolveTextureSample app; return app.run(argc, argv); }
