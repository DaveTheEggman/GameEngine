#include <new>
/// Sample016 — GPU Readback. Ported from Sedulous Sample016_Readback.
/// Renders a colored triangle to a small offscreen texture, copies it to a
/// readback buffer, then reads pixel values on the CPU and prints them.

#include <cstdint>
#include <cstdio>
#include <cstring>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class ReadbackSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample016 - GPU Readback"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = float4(i.Position,1); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return i.Color; }
    )";
    static constexpr raptor::core::u32 kTexSize = 16;
    static constexpr float kVerts[] = {
         0.0f,  1.0f, 0.0f,   1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,   0.0f, 1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f,   0.0f, 0.0f, 1.0f, 1.0f,
    };

    void readbackPixels();

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *offPipeline_ = nullptr, *swapPipeline_ = nullptr;
    dr::Texture *offTex_ = nullptr; dr::TextureView *offView_ = nullptr;
    dr::Buffer *readbackBuf_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    bool hasReadback_ = false;
    float lastReportTime_ = 0.0f;
};

raptor::core::Status ReadbackSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{};
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Small offscreen RGBA8 texture.
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = kTexSize; td.height = kTexSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::CopySrc;
    if (device_->CreateTexture(td, offTex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (device_->CreateTextureView(offTex_, tvd, offView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Readback buffer with row alignment (256 bytes for DX12 compat).
    u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    dr::BufferDesc rbd{}; rbd.size = bytesPerRow * kTexSize; rbd.usage = dr::BufferUsage::CopyDst; rbd.memory = dr::MemoryLocation::GpuToCpu;
    if (device_->CreateBuffer(rbd, readbackBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

    // Pipeline for offscreen (RGBA8Unorm).
    dr::ColorTargetState ct{}; ct.format = dr::TextureFormat::RGBA8Unorm;
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->CreateRenderPipeline(rpd, offPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline for swapchain (different format).
    ct.format = swapChain_->Format();
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->CreateRenderPipeline(rpd, swapPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void ReadbackSample::readbackPixels() {
    void* mapped = readbackBuf_->Map();
    if (!mapped) return;
    raptor::core::u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    auto* data = static_cast<raptor::core::u8*>(mapped);

    std::printf("=== Readback: %ux%u RGBA8 texture ===\n", kTexSize, kTexSize);
    auto printPixel = [&](raptor::core::u32 x, raptor::core::u32 y, const char* label) {
        raptor::core::u32 off = y * bytesPerRow + x * 4;
        std::printf("  %s (%u,%u): R=%u G=%u B=%u A=%u\n", label, x, y,
            data[off], data[off+1], data[off+2], data[off+3]);
    };
    printPixel(0, 0, "Top-left");
    printPixel(kTexSize-1, 0, "Top-right");
    printPixel(kTexSize/2, kTexSize/2, "Center");
    printPixel(0, kTexSize-1, "Bottom-left");
    printPixel(kTexSize-1, kTexSize-1, "Bottom-right");

    int nonBlack = 0;
    for (raptor::core::u32 y = 0; y < kTexSize; ++y)
        for (raptor::core::u32 x = 0; x < kTexSize; ++x) {
            raptor::core::u32 off = y * bytesPerRow + x * 4;
            if (data[off] > 0 || data[off+1] > 0 || data[off+2] > 0) nonBlack++;
        }
    std::printf("Non-black pixels: %d / %u (%.0f%%)\n", nonBlack, kTexSize * kTexSize,
        100.0f * static_cast<float>(nonBlack) / static_cast<float>(kTexSize * kTexSize));
    readbackBuf_->Unmap();
}

void ReadbackSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);

    if (hasReadback_ && (totalTime_ - lastReportTime_ >= 3.0f)) {
        readbackPixels();
        lastReportTime_ = totalTime_;
    }

    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Render triangle to offscreen texture.
    enc->TransitionTexture(offTex_, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = offView_;
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->BeginRenderPass(rpd);
        rp->SetPipeline(offPipeline_);
        rp->SetViewport(0, 0, static_cast<f32>(kTexSize), static_cast<f32>(kTexSize), 0, 1);
        rp->SetScissor(0, 0, kTexSize, kTexSize);
        rp->SetVertexBuffer(0, vb_, 0);
        rp->Draw(3);
        rp->End();
    }
    enc->TransitionTexture(offTex_, dr::ResourceState::RenderTarget, dr::ResourceState::CopySrc);

    // Copy texture to readback buffer.
    raptor::core::u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    dr::BufferTextureCopyRegion region{};
    region.bufferOffset = 0; region.bytesPerRow = bytesPerRow; region.rowsPerImage = kTexSize;
    region.textureExtent = dr::Extent3D{ kTexSize, kTexSize, 1 };
    enc->CopyTextureToBuffer(offTex_, readbackBuf_, region);

    // Also render to swapchain so we see something.
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->BeginRenderPass(rpd);
        rp->SetPipeline(swapPipeline_);
        rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
        rp->SetScissor(0, 0, width_, height_);
        rp->SetVertexBuffer(0, vb_, 0);
        rp->Draw(3);
        rp->End();
    }
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
    hasReadback_ = true;
}

void ReadbackSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (swapPipeline_) device_->DestroyRenderPipeline(swapPipeline_);
    if (offPipeline_) device_->DestroyRenderPipeline(offPipeline_);
    if (pl_) device_->DestroyPipelineLayout(pl_);
    if (readbackBuf_) device_->DestroyBuffer(readbackBuf_);
    if (offView_) device_->DestroyTextureView(offView_);
    if (offTex_) device_->DestroyTexture(offTex_);
    if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { ReadbackSample app; return app.run(argc, argv); }
