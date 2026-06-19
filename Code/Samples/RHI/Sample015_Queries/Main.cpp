#include <new>
/// Sample015 — GPU Queries. Ported from Sedulous Sample015_Queries.
/// Demonstrates GPU timestamp queries to measure render pass duration.
/// Prints render pass GPU time to console every 2 seconds.

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

class QuerySample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::WideStringView Title() const override { return u"Sample015 - GPU Queries"; }
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
    static constexpr float kVerts[] = {
         0.0f,  0.5f, 0.0f,   1.0f, 0.3f, 0.3f, 1.0f,
         0.5f, -0.5f, 0.0f,   0.3f, 1.0f, 0.3f, 1.0f,
        -0.5f, -0.5f, 0.0f,   0.3f, 0.3f, 1.0f, 1.0f,
    };
    static constexpr raptor::core::u16 kIdx[] = { 0, 1, 2 };

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr;
    dr::PipelineLayout *m_pl = nullptr; dr::RenderPipeline *m_pipeline = nullptr;
    dr::QuerySet *m_tsQuerySet = nullptr;
    dr::Buffer *m_queryResultBuf = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
    int m_frameCount = 0;
    float m_lastReportTime = 0.0f;
};

raptor::core::Status QuerySample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{};
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Timestamp query set: 2 queries (before + after render pass).
    dr::QuerySetDesc qsd{}; qsd.type = dr::QueryType::Timestamp; qsd.count = 2;
    if (m_device->CreateQuerySet(qsd, m_tsQuerySet) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffer to receive resolved query results (2 * uint64 = 16 bytes).
    dr::BufferDesc qbd{}; qbd.size = 16; qbd.usage = dr::BufferUsage::CopyDst; qbd.memory = dr::MemoryLocation::GpuToCpu;
    if (m_device->CreateBuffer(qbd, m_queryResultBuf) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void QuerySample::OnRender() {
    using raptor::core::f32, raptor::core::u64, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);

    // Read back previous frame's query results (after fence wait ensures GPU is done).
    if (m_frameCount > 1) {
        void* mapped = m_queryResultBuf->Map();
        if (mapped) {
            auto* timestamps = static_cast<u64*>(mapped);
            u64 begin = timestamps[0], end = timestamps[1], delta = end - begin;
            f32 period = m_graphicsQueue->TimestampPeriod();
            f32 gpuTimeUs = static_cast<f32>(delta) * period / 1000.0f;
            if (m_totalTime - m_lastReportTime >= 2.0f) {
                std::printf("GPU render pass time: %.2f us (%llu ticks, period=%.2f ns)\n",
                    gpuTimeUs, static_cast<unsigned long long>(delta), period);
                m_lastReportTime = m_totalTime;
            }
            m_queryResultBuf->Unmap();
        }
    }

    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Reset queries for this frame.
    enc->ResetQuerySet(m_tsQuerySet, 0, 2);

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    // Timestamp before render pass.
    enc->WriteTimestamp(m_tsQuerySet, 0);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(m_pipeline);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    rp->SetVertexBuffer(0, m_vb, 0);
    rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(3);
    rp->End();

    // Timestamp after render pass.
    enc->WriteTimestamp(m_tsQuerySet, 1);

    // Resolve timestamps to buffer.
    enc->ResolveQuerySet(m_tsQuerySet, 0, 2, m_queryResultBuf, 0);

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
    m_frameCount++;
}

void QuerySample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_queryResultBuf) m_device->DestroyBuffer(m_queryResultBuf);
    if (m_tsQuerySet) m_device->DestroyQuerySet(m_tsQuerySet);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { QuerySample app; return app.Run(argc, argv); }
