#include <new>
/// Sample001 — Triangle. Ported from Sedulous Sample001_Triangle.
/// Renders a colored triangle using vertex buffer + render pipeline.

#include <cstdio>
#include <cstdint>
#include <span>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class TriangleSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample001 - Triangle"; }

protected:
    raptor::core::Status onInit() override;
    void          onRender() override;
    void          onShutdown() override;

private:
    static constexpr const char8_t kShaderSource[] = u8R"(
        struct VSInput {
            float3 Position : TEXCOORD0;
            float3 Color    : TEXCOORD1;
        };
        struct PSInput {
            float4 Position : SV_POSITION;
            float3 Color    : TEXCOORD0;
        };
        PSInput VSMain(VSInput input) {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.Color = input.Color;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET {
            return float4(input.Color, 1.0);
        }
    )";

    static constexpr float kVertexData[] = {
         0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,
    };

    ds::Compiler*       compiler_  = nullptr;
    dr::Buffer*         vertexBuf_ = nullptr;
    dr::ShaderModule*   vs_        = nullptr;
    dr::ShaderModule*   ps_        = nullptr;
    dr::BindGroupLayout* bgl_      = nullptr;
    dr::PipelineLayout*  pl_       = nullptr;
    dr::RenderPipeline*  pipeline_ = nullptr;
    dr::CommandPool*     pool_     = nullptr;
    dr::Fence*           fence_    = nullptr;
    raptor::core::u64           fenceVal_ = 0;
};

raptor::core::Status TriangleSample::onInit() {
    using raptor::core::Status;

    // Shader compiler.
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (sf::compileToModule(compiler_, device_, kShaderSource, ds::ShaderStage::Vertex,   u"VSMain", u"TriangleVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShaderSource, ds::ShaderStage::Fragment, u"PSMain", u"TrianglePS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex buffer.
    dr::BufferDesc bd{}; bd.size = sizeof(kVertexData); bd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst;
    bd.memory = dr::MemoryLocation::GpuOnly; bd.label = u"TriangleVB";
    if (device_->createBuffer(bd, vertexBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Upload.
    dr::TransferBatch* batch = nullptr;
    graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vertexBuf_, 0, raptor::core::Span<const raptor::core::u8>(reinterpret_cast<const raptor::core::u8*>(kVertexData), sizeof(kVertexData)));
    batch->submit();
    graphicsQueue_->destroyTransferBatch(batch);

    // Pipeline layout (empty).
    dr::BindGroupLayoutDesc bglDesc{}; bglDesc.label = u"EmptyBGL";
    if (device_->createBindGroupLayout(bglDesc, bgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::PipelineLayoutDesc pld{};
    dr::BindGroupLayout* sets[1] = { bgl_ };
    pld.bindGroupLayouts = raptor::core::Span<dr::BindGroupLayout* const>(sets, 1);
    pld.label = u"TrianglePL";
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline.
    dr::VertexAttribute attrs[2] = {
        { dr::VertexFormat::Float32x3, 0,  0 },
        { dr::VertexFormat::Float32x3, 12, 1 },
    };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = raptor::core::Span<const dr::VertexAttribute>(attrs, 2);

    dr::ColorTargetState ct{}; ct.format = swapChain_->format(); ct.writeMask = dr::ColorWriteMask::All;

    dr::RenderPipelineDesc rpd{};
    rpd.layout   = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = raptor::core::Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{};
    rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = raptor::core::Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
    rpd.label = u"TrianglePipeline";
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Command pool + fence.
    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    return raptor::core::ErrorCode::Ok;
}

void TriangleSample::onRender() {
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{};
    ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    dr::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);

    auto* rp = enc->beginRenderPass(rpd);
    rp->setPipeline(pipeline_);
    rp->setViewport(0, 0, static_cast<raptor::core::f32>(width_), static_cast<raptor::core::f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vertexBuf_, 0);
    rp->draw(3);
    rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->finish();
    fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(raptor::core::Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);

    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void TriangleSample::onShutdown() {
    if (fence_)    device_->destroyFence(fence_);
    if (pool_)     device_->destroyCommandPool(pool_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_);
    if (pl_)       device_->destroyPipelineLayout(pl_);
    if (bgl_)      device_->destroyBindGroupLayout(bgl_);
    if (ps_)       device_->destroyShaderModule(ps_);
    if (vs_)       device_->destroyShaderModule(vs_);
    if (vertexBuf_) device_->destroyBuffer(vertexBuf_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { TriangleSample app; return app.run(argc, argv); }
