#include <new>
/// Sample007 — Instanced Rendering. Ported from Sedulous Sample007_Instancing.
/// Renders 64 small quads in a grid using instanced draw with per-instance offset + color.
/// Instance buffer is CpuToGpu with per-frame wobble animation.

#include <cmath>
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

struct InstanceData {
    float offset[2];
    float color[4];
};

class InstancingSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample007 - Instanced Rendering"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        struct VSInput
        {
            float3 Position  : TEXCOORD0;
            float2 Offset    : TEXCOORD1;
            float4 InstColor : TEXCOORD2;
        };
        struct PSInput
        {
            float4 Position : SV_POSITION;
            float4 Color    : COLOR0;
        };
        PSInput VSMain(VSInput input)
        {
            PSInput output;
            output.Position = float4(input.Position.xy + input.Offset, input.Position.z, 1.0);
            output.Color = input.InstColor;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET
        {
            return input.Color;
        }
    )";
    static constexpr int kInstanceCount = 64;

    // Unit quad vertices (pos only).
    static constexpr float kQuadVerts[] = {
        -0.04f, -0.04f, 0.0f,
         0.04f, -0.04f, 0.0f,
         0.04f,  0.04f, 0.0f,
        -0.04f,  0.04f, 0.0f,
    };
    static constexpr raptor::core::u16 kQuadIdx[] = { 0, 1, 2, 0, 2, 3 };

    void updateInstances();

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *instBuf_ = nullptr;
    void* instMapped_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *pipeline_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status InstancingSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::f32, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex + index buffers (GpuOnly, static quad geometry).
    dr::BufferDesc vbd{}; vbd.size = sizeof(kQuadVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kQuadIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kQuadVerts), sizeof(kQuadVerts)));
    batch->writeBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kQuadIdx), sizeof(kQuadIdx)));
    batch->submit(); graphicsQueue_->destroyTransferBatch(batch);

    // Instance buffer (CpuToGpu for per-frame updates).
    dr::BufferDesc instBd{}; instBd.size = kInstanceCount * sizeof(InstanceData); instBd.usage = dr::BufferUsage::Vertex; instBd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->createBuffer(instBd, instBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    instMapped_ = instBuf_->map();
    if (!instMapped_) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout (empty — no bind groups needed).
    dr::PipelineLayoutDesc pld{};
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Two vertex buffer layouts: slot 0 = per-vertex, slot 1 = per-instance.
    dr::VertexAttribute vtxAttrs[1] = { {dr::VertexFormat::Float32x3, 0, 0} };
    dr::VertexBufferLayout vtxLayout{}; vtxLayout.stride = 12; vtxLayout.stepMode = dr::VertexStepMode::Vertex;
    vtxLayout.attributes = Span<const dr::VertexAttribute>(vtxAttrs, 1);

    dr::VertexAttribute instAttrs[2] = { {dr::VertexFormat::Float32x2, 0, 1}, {dr::VertexFormat::Float32x4, 8, 2} };
    dr::VertexBufferLayout instLayout{}; instLayout.stride = static_cast<u32>(sizeof(InstanceData)); instLayout.stepMode = dr::VertexStepMode::Instance;
    instLayout.attributes = Span<const dr::VertexAttribute>(instAttrs, 2);

    dr::VertexBufferLayout layouts[2] = { vtxLayout, instLayout };

    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(layouts, 2);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void InstancingSample::updateInstances() {
    auto* data = static_cast<InstanceData*>(instMapped_);
    int gridSize = static_cast<int>(std::sqrt(static_cast<float>(kInstanceCount)));

    for (int i = 0; i < kInstanceCount; ++i) {
        int row = i / gridSize;
        int col = i % gridSize;

        float spacing = 2.0f / static_cast<float>(gridSize);
        float baseX = -1.0f + spacing * 0.5f + col * spacing;
        float baseY = -1.0f + spacing * 0.5f + row * spacing;

        // Animate: wobble in a circle.
        float phase = totalTime_ * 2.0f + i * 0.3f;
        float wobbleX = std::sin(phase) * 0.02f;
        float wobbleY = std::cos(phase * 1.3f) * 0.02f;

        data[i].offset[0] = baseX + wobbleX;
        data[i].offset[1] = baseY + wobbleY;

        // Color: hue based on index.
        float t = static_cast<float>(i) / static_cast<float>(kInstanceCount);
        constexpr float pi2 = 3.14159265f * 2.0f;
        data[i].color[0] = std::abs(std::sin(t * pi2));
        data[i].color[1] = std::abs(std::sin(t * pi2 + 2.094f));
        data[i].color[2] = std::abs(std::sin(t * pi2 + 4.189f));
        data[i].color[3] = 1.0f;
    }
}

void InstancingSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    updateInstances();

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->beginRenderPass(rpd);

    rp->setPipeline(pipeline_);
    rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0);
    rp->setVertexBuffer(1, instBuf_, 0);
    rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->drawIndexed(6, kInstanceCount);
    rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void InstancingSample::onShutdown() {
    if (instBuf_ && instMapped_) instBuf_->unmap();
    if (fence_) device_->destroyFence(fence_); if (pool_) device_->destroyCommandPool(pool_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_); if (pl_) device_->destroyPipelineLayout(pl_);
    if (instBuf_) device_->destroyBuffer(instBuf_);
    if (ib_) device_->destroyBuffer(ib_); if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_); if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { InstancingSample app; return app.run(argc, argv); }
