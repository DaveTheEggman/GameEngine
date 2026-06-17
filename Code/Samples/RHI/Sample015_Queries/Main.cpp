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
    raptor::core::StringView title() const override { return u"Sample015 - GPU Queries"; }
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
    static constexpr float kVerts[] = {
         0.0f,  0.5f, 0.0f,   1.0f, 0.3f, 0.3f, 1.0f,
         0.5f, -0.5f, 0.0f,   0.3f, 1.0f, 0.3f, 1.0f,
        -0.5f, -0.5f, 0.0f,   0.3f, 0.3f, 1.0f, 1.0f,
    };
    static constexpr raptor::core::u16 kIdx[] = { 0, 1, 2 };

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr; dr::RenderPipeline *pipeline_ = nullptr;
    dr::QuerySet *tsQuerySet_ = nullptr;
    dr::Buffer *queryResultBuf_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    int frameCount_ = 0;
    float lastReportTime_ = 0.0f;
};

raptor::core::Status QuerySample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->writeBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->submit(); graphicsQueue_->destroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{};
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Timestamp query set: 2 queries (before + after render pass).
    dr::QuerySetDesc qsd{}; qsd.type = dr::QueryType::Timestamp; qsd.count = 2;
    if (device_->createQuerySet(qsd, tsQuerySet_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffer to receive resolved query results (2 * uint64 = 16 bytes).
    dr::BufferDesc qbd{}; qbd.size = 16; qbd.usage = dr::BufferUsage::CopyDst; qbd.memory = dr::MemoryLocation::GpuToCpu;
    if (device_->createBuffer(qbd, queryResultBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void QuerySample::onRender() {
    using raptor::core::f32, raptor::core::u64, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);

    // Read back previous frame's query results (after fence wait ensures GPU is done).
    if (frameCount_ > 1) {
        void* mapped = queryResultBuf_->map();
        if (mapped) {
            auto* timestamps = static_cast<u64*>(mapped);
            u64 begin = timestamps[0], end = timestamps[1], delta = end - begin;
            f32 period = graphicsQueue_->timestampPeriod();
            f32 gpuTimeUs = static_cast<f32>(delta) * period / 1000.0f;
            if (totalTime_ - lastReportTime_ >= 2.0f) {
                std::printf("GPU render pass time: %.2f us (%llu ticks, period=%.2f ns)\n",
                    gpuTimeUs, static_cast<unsigned long long>(delta), period);
                lastReportTime_ = totalTime_;
            }
            queryResultBuf_->unmap();
        }
    }

    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Reset queries for this frame.
    enc->resetQuerySet(tsQuerySet_, 0, 2);

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    // Timestamp before render pass.
    enc->writeTimestamp(tsQuerySet_, 0);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->beginRenderPass(rpd);
    rp->setPipeline(pipeline_);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0);
    rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->drawIndexed(3);
    rp->end();

    // Timestamp after render pass.
    enc->writeTimestamp(tsQuerySet_, 1);

    // Resolve timestamps to buffer.
    enc->resolveQuerySet(tsQuerySet_, 0, 2, queryResultBuf_, 0);

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
    frameCount_++;
}

void QuerySample::onShutdown() {
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (queryResultBuf_) device_->destroyBuffer(queryResultBuf_);
    if (tsQuerySet_) device_->destroyQuerySet(tsQuerySet_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_); if (pl_) device_->destroyPipelineLayout(pl_);
    if (ib_) device_->destroyBuffer(ib_); if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { QuerySample app; return app.run(argc, argv); }
