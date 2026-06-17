#include <new>
/// Sample022 — Stencil Outline. Ported from Sedulous Sample022_StencilOutline.
/// Demonstrates stencil buffer operations for object outlining.
/// Pass 1: Draw solid hexagon, write stencil = 1.
/// Pass 2: Draw scaled-up hexagon, only where stencil != 1 (outline effect).

#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class StencilOutlineSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample022 - Stencil Outline"; }

protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { recreateDepthStencil(w, h); }
    void onShutdown() override;

private:
    static constexpr const char8_t kShader[] = u8R"(
        struct PushConstants
        {
            float Scale;
            float AspectRatio;
            float Time;
            float _pad;
        };

        [[vk::push_constant]] ConstantBuffer<PushConstants> pc : register(b0, space0);

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
            float2 pos = input.Position.xy * pc.Scale;
            pos.x /= pc.AspectRatio;
            // Gentle rotation
            float c = cos(pc.Time * 0.5);
            float s = sin(pc.Time * 0.5);
            float2 rotated = float2(pos.x * c - pos.y * s, pos.x * s + pos.y * c);
            output.Position = float4(rotated, input.Position.z, 1.0);
            output.Color = input.Color;
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            return input.Color;
        }
    )";

    struct PushData {
        float scale;
        float aspectRatio;
        float time;
        float _pad;
    };

    // Hexagon: center + 6 outer vertices.
    // Stride: 7 floats per vertex (pos xyz + color rgba).
    static constexpr float kVerts[] = {
        // Center
        0.0f, 0.0f, 0.5f,   0.9f, 0.9f, 0.9f, 1.0f,
        // Outer vertices (radius 0.6)
         0.6f,   0.0f,   0.5f,   0.3f, 0.6f, 1.0f, 1.0f,
         0.3f,   0.52f,  0.5f,   0.3f, 1.0f, 0.6f, 1.0f,
        -0.3f,   0.52f,  0.5f,   1.0f, 1.0f, 0.3f, 1.0f,
        -0.6f,   0.0f,   0.5f,   1.0f, 0.6f, 0.3f, 1.0f,
        -0.3f,  -0.52f,  0.5f,   1.0f, 0.3f, 0.6f, 1.0f,
         0.3f,  -0.52f,  0.5f,   0.6f, 0.3f, 1.0f, 1.0f,
    };
    static constexpr raptor::core::u16 kIdx[] = {
        0, 1, 2,
        0, 2, 3,
        0, 3, 4,
        0, 4, 5,
        0, 5, 6,
        0, 6, 1,
    };

    void recreateDepthStencil(raptor::core::u32 w, raptor::core::u32 h);

    ds::Compiler* compiler_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::PipelineLayout* pl_ = nullptr;
    dr::RenderPipeline* stencilWritePipeline_ = nullptr;
    dr::RenderPipeline* stencilTestPipeline_ = nullptr;
    dr::Texture* depthStencilTex_ = nullptr;
    dr::TextureView* depthStencilView_ = nullptr;
    dr::CommandPool* pool_ = nullptr;
    dr::Fence* fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

void StencilOutlineSample::recreateDepthStencil(raptor::core::u32 w, raptor::core::u32 h) {
    if (depthStencilView_) { device_->DestroyTextureView(depthStencilView_); depthStencilView_ = nullptr; }
    if (depthStencilTex_) { device_->DestroyTexture(depthStencilTex_); depthStencilTex_ = nullptr; }

    dr::TextureDesc td = dr::TextureDesc::DepthBuffer(dr::TextureFormat::Depth24PlusStencil8, w, h, 1, u"StencilDSTex");
    device_->CreateTexture(td, depthStencilTex_);
    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::Depth24PlusStencil8; tvd.dimension = dr::TextureViewDimension::Texture2D;
    tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    device_->CreateTextureView(depthStencilTex_, tvd, depthStencilView_);
}

raptor::core::Status StencilOutlineSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"StencilVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"StencilPS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex & index buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kVerts), sizeof(kVerts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kIdx), sizeof(kIdx)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    // Pipeline layout with push constants (no bind groups).
    dr::PushConstantRange pcRange{ dr::ShaderStage::Vertex, 0, sizeof(PushData) };
    dr::PipelineLayoutDesc pld{};
    pld.pushConstantRanges = Span<const dr::PushConstantRange>(&pcRange, 1);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    recreateDepthStencil(width_, height_);

    // Shared vertex layout.
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();

    // Pipeline 1: Stencil write — draw solid, always pass depth, write stencil = ref (1).
    {
        dr::RenderPipelineDesc rpd{};
        rpd.layout = pl_;
        rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
        rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
        rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.depthStencil = dr::DepthStencilState{};
        rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
        rpd.depthStencil->depthWriteEnabled = true;
        rpd.depthStencil->depthCompare = dr::CompareFunction::Always;
        rpd.depthStencil->stencilEnabled = true;
        rpd.depthStencil->stencilReadMask = 0xFF;
        rpd.depthStencil->stencilWriteMask = 0xFF;
        rpd.depthStencil->stencilFront = { dr::CompareFunction::Always, dr::StencilOperation::Keep, dr::StencilOperation::Keep, dr::StencilOperation::Replace };
        rpd.depthStencil->stencilBack  = { dr::CompareFunction::Always, dr::StencilOperation::Keep, dr::StencilOperation::Keep, dr::StencilOperation::Replace };
        if (device_->CreateRenderPipeline(rpd, stencilWritePipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Pipeline 2: Stencil test — draw outline, only where stencil != 1.
    {
        dr::RenderPipelineDesc rpd{};
        rpd.layout = pl_;
        rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
        rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
        rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.depthStencil = dr::DepthStencilState{};
        rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
        rpd.depthStencil->depthWriteEnabled = false;
        rpd.depthStencil->depthCompare = dr::CompareFunction::Always;
        rpd.depthStencil->stencilEnabled = true;
        rpd.depthStencil->stencilReadMask = 0xFF;
        rpd.depthStencil->stencilWriteMask = 0x00;
        rpd.depthStencil->stencilFront = { dr::CompareFunction::NotEqual, dr::StencilOperation::Keep, dr::StencilOperation::Keep, dr::StencilOperation::Keep };
        rpd.depthStencil->stencilBack  = { dr::CompareFunction::NotEqual, dr::StencilOperation::Keep, dr::StencilOperation::Keep, dr::StencilOperation::Keep };
        if (device_->CreateRenderPipeline(rpd, stencilTestPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void StencilOutlineSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(depthStencilTex_, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    dr::DepthStencilAttachment dsa{}; dsa.view = depthStencilView_;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dsa.stencilLoadOp = dr::LoadOp::Clear; dsa.stencilStoreOp = dr::StoreOp::Store; dsa.stencilClearValue = 0;

    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);

    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);

    // Pass 1: Draw solid hexagon, write stencil = 1.
    rp->SetPipeline(stencilWritePipeline_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0.0f, 1.0f);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->SetStencilReference(1);
    PushData pc1{ 1.0f, aspect, totalTime_, 0.0f };
    rp->SetPushConstants(dr::ShaderStage::Vertex, 0, sizeof(PushData), &pc1);
    rp->DrawIndexed(18);

    // Pass 2: Draw scaled-up hexagon, only where stencil != 1 (outline ring).
    rp->SetPipeline(stencilTestPipeline_);
    rp->SetStencilReference(1);
    PushData pc2{ 1.15f, aspect, totalTime_, 0.0f };
    rp->SetPushConstants(dr::ShaderStage::Vertex, 0, sizeof(PushData), &pc2);
    rp->DrawIndexed(18);

    rp->End();

    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void StencilOutlineSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (stencilTestPipeline_) device_->DestroyRenderPipeline(stencilTestPipeline_);
    if (stencilWritePipeline_) device_->DestroyRenderPipeline(stencilWritePipeline_);
    if (depthStencilView_) device_->DestroyTextureView(depthStencilView_);
    if (depthStencilTex_) device_->DestroyTexture(depthStencilTex_);
    if (pl_) device_->DestroyPipelineLayout(pl_);
    if (ps_) device_->DestroyShaderModule(ps_);
    if (vs_) device_->DestroyShaderModule(vs_);
    if (ib_) device_->DestroyBuffer(ib_);
    if (vb_) device_->DestroyBuffer(vb_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { StencilOutlineSample app; return app.run(argc, argv); }
