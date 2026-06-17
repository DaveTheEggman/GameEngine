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
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->submit(); graphicsQueue_->destroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{};
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Small offscreen RGBA8 texture.
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = kTexSize; td.height = kTexSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::CopySrc;
    if (device_->createTexture(td, offTex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (device_->createTextureView(offTex_, tvd, offView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Readback buffer with row alignment (256 bytes for DX12 compat).
    u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    dr::BufferDesc rbd{}; rbd.size = bytesPerRow * kTexSize; rbd.usage = dr::BufferUsage::CopyDst; rbd.memory = dr::MemoryLocation::GpuToCpu;
    if (device_->createBuffer(rbd, readbackBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

    // Pipeline for offscreen (RGBA8Unorm).
    dr::ColorTargetState ct{}; ct.format = dr::TextureFormat::RGBA8Unorm;
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->createRenderPipeline(rpd, offPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline for swapchain (different format).
    ct.format = swapChain_->format();
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->createRenderPipeline(rpd, swapPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void ReadbackSample::readbackPixels() {
    void* mapped = readbackBuf_->map();
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
    readbackBuf_->unmap();
}

void ReadbackSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);

    if (hasReadback_ && (totalTime_ - lastReportTime_ >= 3.0f)) {
        readbackPixels();
        lastReportTime_ = totalTime_;
    }

    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Render triangle to offscreen texture.
    enc->transitionTexture(offTex_, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = offView_;
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->beginRenderPass(rpd);
        rp->setPipeline(offPipeline_);
        rp->setViewport(0, 0, static_cast<f32>(kTexSize), static_cast<f32>(kTexSize), 0, 1);
        rp->setScissor(0, 0, kTexSize, kTexSize);
        rp->setVertexBuffer(0, vb_, 0);
        rp->draw(3);
        rp->end();
    }
    enc->transitionTexture(offTex_, dr::ResourceState::RenderTarget, dr::ResourceState::CopySrc);

    // Copy texture to readback buffer.
    raptor::core::u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    dr::BufferTextureCopyRegion region{};
    region.bufferOffset = 0; region.bytesPerRow = bytesPerRow; region.rowsPerImage = kTexSize;
    region.textureExtent = dr::Extent3D{ kTexSize, kTexSize, 1 };
    enc->copyTextureToBuffer(offTex_, readbackBuf_, region);

    // Also render to swapchain so we see something.
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->beginRenderPass(rpd);
        rp->setPipeline(swapPipeline_);
        rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
        rp->setScissor(0, 0, width_, height_);
        rp->setVertexBuffer(0, vb_, 0);
        rp->draw(3);
        rp->end();
    }
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
    hasReadback_ = true;
}

void ReadbackSample::onShutdown() {
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (swapPipeline_) device_->destroyRenderPipeline(swapPipeline_);
    if (offPipeline_) device_->destroyRenderPipeline(offPipeline_);
    if (pl_) device_->destroyPipelineLayout(pl_);
    if (readbackBuf_) device_->destroyBuffer(readbackBuf_);
    if (offView_) device_->destroyTextureView(offView_);
    if (offTex_) device_->destroyTexture(offTex_);
    if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { ReadbackSample app; return app.run(argc, argv); }
