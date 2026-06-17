#include <new>
/// Sample024 — Occlusion Queries & Debug Labels. Ported from Sedulous Sample024_OcclusionQuery.
/// Demonstrates occlusion queries and debug labels.
/// Renders an occluder quad, then two test quads behind it with occlusion queries.
/// Prints pixel counts to console. Uses debug labels to mark render sections.

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

class OcclusionQuerySample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample024 - Occlusion Queries & Debug Labels"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateDepth(w, h); }
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput
        {
            float3 Position : TEXCOORD0;
            float4 Color    : TEXCOORD1;
        };

        struct PSInput
        {
            float4 Position : SV_POSITION;
            float4 Color    : COLOR0;
        };

        PSInput VSMain(VSInput input)
        {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.Color = input.Color;
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            return input.Color;
        }
    )";

    // Geometry: 3 quads
    // Quad 0: Occluder (opaque gray, z=0.3, center)
    // Quad 1: Test A (red, z=0.7, partially behind occluder)
    // Quad 2: Test B (blue, z=0.7, fully behind occluder)
    static constexpr float kVerts[] = {
        // Quad 0: Occluder - center, near
        -0.3f, -0.4f, 0.3f,   0.4f, 0.4f, 0.4f, 1.0f,
         0.3f, -0.4f, 0.3f,   0.4f, 0.4f, 0.4f, 1.0f,
         0.3f,  0.4f, 0.3f,   0.5f, 0.5f, 0.5f, 1.0f,
        -0.3f,  0.4f, 0.3f,   0.5f, 0.5f, 0.5f, 1.0f,

        // Quad 1: Test A - partially occluded (left side visible)
        -0.7f, -0.3f, 0.7f,   1.0f, 0.3f, 0.3f, 1.0f,
         0.0f, -0.3f, 0.7f,   1.0f, 0.3f, 0.3f, 1.0f,
         0.0f,  0.3f, 0.7f,   1.0f, 0.5f, 0.5f, 1.0f,
        -0.7f,  0.3f, 0.7f,   1.0f, 0.5f, 0.5f, 1.0f,

        // Quad 2: Test B - fully occluded (behind occluder)
        -0.15f, -0.2f, 0.7f,  0.3f, 0.3f, 1.0f, 1.0f,
         0.15f, -0.2f, 0.7f,  0.3f, 0.3f, 1.0f, 1.0f,
         0.15f,  0.2f, 0.7f,  0.5f, 0.5f, 1.0f, 1.0f,
        -0.15f,  0.2f, 0.7f,  0.5f, 0.5f, 1.0f, 1.0f,
    };
    static constexpr raptor::core::u16 kIdx[] = {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
    };

    void recreateDepth(raptor::core::u32 w, raptor::core::u32 h);

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *pipeline_ = nullptr;
    dr::Texture *depthTex_ = nullptr;
    dr::TextureView *depthView_ = nullptr;
    dr::QuerySet *occlusionQuerySet_ = nullptr;
    dr::Buffer *queryResultBuf_ = nullptr;
    dr::CommandPool *pool_ = nullptr;
    dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    int frameCount_ = 0;
    float lastReportTime_ = 0.0f;
};

void OcclusionQuerySample::recreateDepth(raptor::core::u32 w, raptor::core::u32 h) {
    if (depthView_) { device_->destroyTextureView(depthView_); depthView_ = nullptr; }
    if (depthTex_) { device_->destroyTexture(depthTex_); depthTex_ = nullptr; }

    dr::TextureDesc td = dr::TextureDesc::depthBuffer(dr::TextureFormat::Depth24PlusStencil8, w, h, 1, u"OccDepthTex");
    device_->createTexture(td, depthTex_);
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::Depth24PlusStencil8; tvd.dimension = dr::TextureViewDimension::Texture2D;
    tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    device_->createTextureView(depthTex_, tvd, depthView_);
}

raptor::core::Status OcclusionQuerySample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"OccVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"OccPS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly; vbd.label = u"OccVB";
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly; ibd.label = u"OccIB";
    if (device_->createBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->writeBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->submit(); graphicsQueue_->destroyTransferBatch(batch);

    dr::PipelineLayoutDesc pld{}; pld.label = u"OccPL";
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    recreateDepth(width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthWriteEnabled = true;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    rpd.label = u"OccPipeline";
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Occlusion query set: 2 queries (one per test quad).
    dr::QuerySetDesc qsd{}; qsd.type = dr::QueryType::Occlusion; qsd.count = 2; qsd.label = u"OcclusionQS";
    if (device_->createQuerySet(qsd, occlusionQuerySet_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffer for query results (2 * uint64 = 16 bytes).
    dr::BufferDesc qbd{}; qbd.size = 16; qbd.usage = dr::BufferUsage::CopyDst; qbd.memory = dr::MemoryLocation::GpuToCpu; qbd.label = u"OccResultBuf";
    if (device_->createBuffer(qbd, queryResultBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void OcclusionQuerySample::onRender() {
    using raptor::core::f32, raptor::core::u64, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);

    // Read back previous frame's occlusion results (after fence wait ensures GPU is done).
    if (frameCount_ > 1) {
        void* mapped = queryResultBuf_->map();
        if (mapped) {
            auto* results = static_cast<u64*>(mapped);
            u64 pixelsA = results[0];
            u64 pixelsB = results[1];

            if (totalTime_ - lastReportTime_ >= 2.0f) {
                std::printf("Occlusion: QuadA=%llu pixels, QuadB=%llu pixels (B should be ~0)\n",
                    static_cast<unsigned long long>(pixelsA), static_cast<unsigned long long>(pixelsB));
                lastReportTime_ = totalTime_;
            }
            queryResultBuf_->unmap();
        }
    }

    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    // Debug label: frame start.
    enc->insertDebugLabel(u"Frame Start", 0, 1, 0);

    // Reset queries for this frame.
    enc->resetQuerySet(occlusionQuerySet_, 0, 2);

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->transitionTexture(depthTex_, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    // Debug label: render pass.
    enc->beginDebugLabel(u"Main Render Pass", 0.2f, 0.5f, 1.0f);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthView_;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->beginRenderPass(rpd);

    rp->setPipeline(pipeline_);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0);
    rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);

    // Draw occluder first (writes depth).
    rp->drawIndexed(6, 1, 0, 0, 0);

    // Draw test quad A with occlusion query 0.
    rp->beginOcclusionQuery(occlusionQuerySet_, 0);
    rp->drawIndexed(6, 1, 6, 0, 0);
    rp->endOcclusionQuery(occlusionQuerySet_, 0);

    // Draw test quad B with occlusion query 1.
    rp->beginOcclusionQuery(occlusionQuerySet_, 1);
    rp->drawIndexed(6, 1, 12, 0, 0);
    rp->endOcclusionQuery(occlusionQuerySet_, 1);

    rp->end();

    enc->endDebugLabel();

    // Resolve occlusion queries to buffer.
    enc->resolveQuerySet(occlusionQuerySet_, 0, 2, queryResultBuf_, 0);

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);

    frameCount_++;
}

void OcclusionQuerySample::onShutdown() {
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (queryResultBuf_) device_->destroyBuffer(queryResultBuf_);
    if (occlusionQuerySet_) device_->destroyQuerySet(occlusionQuerySet_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_); if (pl_) device_->destroyPipelineLayout(pl_);
    if (depthView_) device_->destroyTextureView(depthView_);
    if (depthTex_) device_->destroyTexture(depthTex_);
    if (ib_) device_->destroyBuffer(ib_); if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { OcclusionQuerySample app; return app.run(argc, argv); }
