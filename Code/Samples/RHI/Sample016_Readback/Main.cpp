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
    raptor::core::StringView Title() const override { return u8"Sample016 - GPU Readback"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
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

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr;
    dr::PipelineLayout *m_pl = nullptr;
    dr::RenderPipeline *m_offPipeline = nullptr, *m_swapPipeline = nullptr;
    dr::Texture *m_offTex = nullptr; dr::TextureView *m_offView = nullptr;
    dr::Buffer *m_readbackBuf = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
    bool m_hasReadback = false;
    float m_lastReportTime = 0.0f;
};

raptor::core::Status ReadbackSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"VS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u8"PSMain", u8"PS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{};
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Small offscreen RGBA8 texture.
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = kTexSize; td.height = kTexSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::RenderTarget | dr::TextureUsage::CopySrc;
    if (m_device->CreateTexture(td, m_offTex) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (m_device->CreateTextureView(m_offTex, tvd, m_offView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Readback buffer with row alignment (256 bytes for DX12 compat).
    u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    dr::BufferDesc rbd{}; rbd.size = bytesPerRow * kTexSize; rbd.usage = dr::BufferUsage::CopyDst; rbd.memory = dr::MemoryLocation::GpuToCpu;
    if (m_device->CreateBuffer(rbd, m_readbackBuf) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

    // Pipeline for offscreen (RGBA8Unorm).
    dr::ColorTargetState ct{}; ct.format = dr::TextureFormat::RGBA8Unorm;
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u8"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u8"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_offPipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline for swapchain (different format).
    ct.format = m_swapChain->Format();
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_swapPipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void ReadbackSample::readbackPixels() {
    void* mapped = m_readbackBuf->Map();
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
    m_readbackBuf->Unmap();
}

void ReadbackSample::OnRender() {
    using raptor::core::f32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);

    if (m_hasReadback && (m_totalTime - m_lastReportTime >= 3.0f)) {
        readbackPixels();
        m_lastReportTime = m_totalTime;
    }

    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Render triangle to offscreen texture.
    enc->TransitionTexture(m_offTex, dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = m_offView;
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->BeginRenderPass(rpd);
        rp->SetPipeline(m_offPipeline);
        rp->SetViewport(0, 0, static_cast<f32>(kTexSize), static_cast<f32>(kTexSize), 0, 1);
        rp->SetScissor(0, 0, kTexSize, kTexSize);
        rp->SetVertexBuffer(0, m_vb, 0);
        rp->Draw(3);
        rp->End();
    }
    enc->TransitionTexture(m_offTex, dr::ResourceState::RenderTarget, dr::ResourceState::CopySrc);

    // Copy texture to readback buffer.
    raptor::core::u32 bytesPerRow = ((kTexSize * 4 + 255) / 256) * 256;
    dr::BufferTextureCopyRegion region{};
    region.bufferOffset = 0; region.bytesPerRow = bytesPerRow; region.rowsPerImage = kTexSize;
    region.textureExtent = dr::Extent3D{ kTexSize, kTexSize, 1 };
    enc->CopyTextureToBuffer(m_offTex, m_readbackBuf, region);

    // Also render to swapchain so we see something.
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    {
        dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
        ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
        ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
        auto* rp = enc->BeginRenderPass(rpd);
        rp->SetPipeline(m_swapPipeline);
        rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1);
        rp->SetScissor(0, 0, m_width, m_height);
        rp->SetVertexBuffer(0, m_vb, 0);
        rp->Draw(3);
        rp->End();
    }
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
    m_hasReadback = true;
}

void ReadbackSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_swapPipeline) m_device->DestroyRenderPipeline(m_swapPipeline);
    if (m_offPipeline) m_device->DestroyRenderPipeline(m_offPipeline);
    if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_readbackBuf) m_device->DestroyBuffer(m_readbackBuf);
    if (m_offView) m_device->DestroyTextureView(m_offView);
    if (m_offTex) m_device->DestroyTexture(m_offTex);
    if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { ReadbackSample app; return app.Run(argc, argv); }
