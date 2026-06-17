#include <new>
/// Sample025 — Multi-Draw Indirect & Lines. Ported from Sedulous Sample025_MultiDrawIndirect.
/// Renders 4 colored quads using a single drawIndexedIndirect call with drawCount=4,
/// then overlays white line wireframes using LineList topology.

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

struct DrawIndexedIndirectArgs {
    raptor::core::u32 indexCountPerInstance;
    raptor::core::u32 instanceCount;
    raptor::core::u32 startIndexLocation;
    raptor::core::i32 baseVertexLocation;
    raptor::core::u32 startInstanceLocation;
};

class MultiDrawIndirectSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample025 - Multi-Draw Indirect & Lines"; }
protected:
    dr::DeviceFeatures requiredFeatures() const override {
        dr::DeviceFeatures f{};
        f.multiDrawIndirect = true;
        return f;
    }
    raptor::core::Status onInit() override;
    void onRender() override;
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

    raptor::core::Status createGeometry();
    raptor::core::Status createIndirectBuffer();
    raptor::core::Status createLineGeometry();

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *indirectBuf_ = nullptr, *lineVb_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *fillPipeline_ = nullptr, *linePipeline_ = nullptr;
    dr::CommandPool *pool_ = nullptr;
    dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status MultiDrawIndirectSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (createGeometry() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (createIndirectBuffer() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (createLineGeometry() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout (empty — no bind groups needed).
    dr::PipelineLayoutDesc pld{};
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex layout: pos float3 + color float4, stride 28.
    dr::VertexAttribute attrs[2] = {
        {dr::VertexFormat::Float32x3,  0, 0},
        {dr::VertexFormat::Float32x4, 12, 1}
    };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.stepMode = dr::VertexStepMode::Vertex;
    vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();

    // Fill pipeline (TriangleList).
    {
        dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
        rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
        rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
        rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
        if (device_->CreateRenderPipeline(rpd, fillPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Line pipeline (LineList).
    {
        dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
        rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
        rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
        rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::LineList;
        if (device_->CreateRenderPipeline(rpd, linePipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status MultiDrawIndirectSample::createGeometry() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    // 4 quads at different positions.
    static constexpr float verts[] = {
        // Quad 0: top-left (red)
        -0.9f,  0.1f, 0.5f,   0.8f, 0.2f, 0.2f, 1.0f,
        -0.1f,  0.1f, 0.5f,   0.8f, 0.2f, 0.2f, 1.0f,
        -0.1f,  0.9f, 0.5f,   1.0f, 0.4f, 0.4f, 1.0f,
        -0.9f,  0.9f, 0.5f,   1.0f, 0.4f, 0.4f, 1.0f,

        // Quad 1: top-right (green)
         0.1f,  0.1f, 0.5f,   0.2f, 0.8f, 0.2f, 1.0f,
         0.9f,  0.1f, 0.5f,   0.2f, 0.8f, 0.2f, 1.0f,
         0.9f,  0.9f, 0.5f,   0.4f, 1.0f, 0.4f, 1.0f,
         0.1f,  0.9f, 0.5f,   0.4f, 1.0f, 0.4f, 1.0f,

        // Quad 2: bottom-left (blue)
        -0.9f, -0.9f, 0.5f,   0.2f, 0.2f, 0.8f, 1.0f,
        -0.1f, -0.9f, 0.5f,   0.2f, 0.2f, 0.8f, 1.0f,
        -0.1f, -0.1f, 0.5f,   0.4f, 0.4f, 1.0f, 1.0f,
        -0.9f, -0.1f, 0.5f,   0.4f, 0.4f, 1.0f, 1.0f,

        // Quad 3: bottom-right (yellow)
         0.1f, -0.9f, 0.5f,   0.8f, 0.8f, 0.2f, 1.0f,
         0.9f, -0.9f, 0.5f,   0.8f, 0.8f, 0.2f, 1.0f,
         0.9f, -0.1f, 0.5f,   1.0f, 1.0f, 0.4f, 1.0f,
         0.1f, -0.1f, 0.5f,   1.0f, 1.0f, 0.4f, 1.0f,
    };

    static constexpr raptor::core::u16 indices[] = {
         0,  1,  2,  0,  2,  3,   // Quad 0
         4,  5,  6,  4,  6,  7,   // Quad 1
         8,  9, 10,  8, 10, 11,   // Quad 2
        12, 13, 14, 12, 14, 15,   // Quad 3
    };

    dr::BufferDesc vbd{}; vbd.size = sizeof(verts);
    vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst;
    vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc ibd{}; ibd.size = sizeof(indices);
    ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst;
    ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(verts), sizeof(verts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(indices), sizeof(indices)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status MultiDrawIndirectSample::createIndirectBuffer() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;

    // 4 indirect draw commands — one per quad.
    DrawIndexedIndirectArgs args[4] = {
        { 6, 1,  0, 0, 0 },
        { 6, 1,  6, 0, 0 },
        { 6, 1, 12, 0, 0 },
        { 6, 1, 18, 0, 0 },
    };

    dr::BufferDesc bd{}; bd.size = sizeof(args);
    bd.usage = dr::BufferUsage::Indirect | dr::BufferUsage::CopyDst;
    bd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(bd, indirectBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(indirectBuf_, 0, Span<const u8>(reinterpret_cast<const u8*>(args), sizeof(args)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status MultiDrawIndirectSample::createLineGeometry() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;

    // Line wireframes for each quad: 4 edges per quad = 8 verts per quad, white lines.
    static constexpr float lineVerts[] = {
        // Quad 0 edges
        -0.9f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.9f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.9f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.9f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,

        // Quad 1 edges
         0.1f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.1f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.1f,  0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.1f,  0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,

        // Quad 2 edges
        -0.9f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.1f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.9f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.9f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
        -0.9f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,

        // Quad 3 edges
         0.1f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.9f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.1f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.1f, -0.1f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
         0.1f, -0.9f, 0.4f,   1.0f, 1.0f, 1.0f, 1.0f,
    };

    dr::BufferDesc bd{}; bd.size = sizeof(lineVerts);
    bd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst;
    bd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(bd, lineVb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(lineVb_, 0, Span<const u8>(reinterpret_cast<const u8*>(lineVerts), sizeof(lineVerts)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    return raptor::core::ErrorCode::Ok;
}

void MultiDrawIndirectSample::onRender() {
    using raptor::core::f32, raptor::core::Span, raptor::core::u32;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.06f, 0.06f, 0.1f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0.0f, 1.0f);
    rp->SetScissor(0, 0, width_, height_);

    // Pass 1: Draw all 4 quads with a single multi-draw indirect call.
    rp->SetPipeline(fillPipeline_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexedIndirect(indirectBuf_, 0, 4, static_cast<u32>(sizeof(DrawIndexedIndirectArgs)));

    // Pass 2: Draw line wireframes.
    rp->SetPipeline(linePipeline_);
    rp->SetVertexBuffer(0, lineVb_, 0);
    rp->Draw(32); // 4 quads * 4 edges * 2 verts = 32 line verts

    rp->End();

    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void MultiDrawIndirectSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (linePipeline_) device_->DestroyRenderPipeline(linePipeline_);
    if (fillPipeline_) device_->DestroyRenderPipeline(fillPipeline_);
    if (pl_) device_->DestroyPipelineLayout(pl_);
    if (ps_) device_->DestroyShaderModule(ps_);
    if (vs_) device_->DestroyShaderModule(vs_);
    if (lineVb_) device_->DestroyBuffer(lineVb_);
    if (indirectBuf_) device_->DestroyBuffer(indirectBuf_);
    if (ib_) device_->DestroyBuffer(ib_);
    if (vb_) device_->DestroyBuffer(vb_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { MultiDrawIndirectSample app; return app.run(argc, argv); }
