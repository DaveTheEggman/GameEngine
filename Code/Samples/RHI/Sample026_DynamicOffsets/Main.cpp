#include <new>
/// Sample026 — Dynamic Offsets & Blend Constants. Ported from Sedulous Sample026_DynamicOffsets.
/// Demonstrates dynamic uniform buffer offsets and blend constants.
/// Draws 4 quads, each reading from a different offset in one shared UBO.
/// Uses setBlendConstant with BlendFactor::Constant for per-frame color modulation.

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

class DynamicOffsetSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample026 - Dynamic Offsets & Blend Constants"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    void updateUBO();

    static constexpr const char8_t kShader[] = u8R"(
        cbuffer ObjectData : register(b0, space0)
        {
            float4 TintColor;
            float4 OffsetScale; // xy=offset, zw=scale
        };

        struct VSInput
        {
            float3 Position : TEXCOORD0;
        };

        struct PSInput
        {
            float4 Position : SV_POSITION;
        };

        PSInput VSMain(VSInput input)
        {
            PSInput output;
            float2 pos = input.Position.xy * OffsetScale.zw + OffsetScale.xy;
            output.Position = float4(pos, input.Position.z, 1.0);
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            return TintColor;
        }
    )";

    struct ObjectData {
        float tintColor[4];
        float offsetScale[4];
        // Pad to 256-byte alignment (D3D12 CBV minimum).
        float _pad[56];
    };

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *ub_ = nullptr;
    dr::BindGroupLayout *bgl_ = nullptr;
    dr::BindGroup *bg_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *pipeline_ = nullptr;
    dr::CommandPool *pool_ = nullptr;
    dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
};

raptor::core::Status DynamicOffsetSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"DynVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"DynPS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Unit quad vertices (will be transformed by UBO data).
    static constexpr float verts[] = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f,
    };
    static constexpr raptor::core::u16 indices[] = { 0, 1, 2, 0, 2, 3 };

    dr::BufferDesc vbd{}; vbd.size = sizeof(verts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(indices); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(verts), sizeof(verts)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(indices), sizeof(indices)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    // Uniform buffer: 4 ObjectData structs (256 bytes each = 1024 total).
    dr::BufferDesc ubd{}; ubd.size = 256 * 4; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(ubd, ub_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Initialize UBO data.
    updateUBO();

    // Bind group layout with dynamic offset UBO.
    dr::BindGroupLayoutEntry entry = dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment);
    entry.hasDynamicOffset = true;
    dr::BindGroupLayoutEntry entries[1] = { entry };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(entries, 1);
    if (device_->CreateBindGroupLayout(bgld, bgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind group (bind the whole buffer, dynamic offset selects the slice).
    dr::BindGroupEntry bgEntries[1] = { dr::BindGroupEntry::BufferEntry(ub_, 0, 256) };
    dr::BindGroupDesc bgd{}; bgd.layout = bgl_; bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 1);
    if (device_->CreateBindGroup(bgd, bg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout.
    dr::BindGroupLayout* sets[1] = { bgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline with blend constant support.
    dr::VertexAttribute attrs[1] = { { dr::VertexFormat::Float32x3, 0, 0 } };
    dr::VertexBufferLayout vbl{}; vbl.stride = 12; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 1);

    dr::ColorTargetState ct{}; ct.format = swapChain_->Format(); ct.writeMask = dr::ColorWriteMask::All;
    ct.blend = dr::BlendState{
        { dr::BlendFactor::Constant, dr::BlendFactor::OneMinusConstant, dr::BlendOperation::Add },
        { dr::BlendFactor::One,      dr::BlendFactor::Zero,             dr::BlendOperation::Add }
    };

    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void DynamicOffsetSample::updateUBO() {
    void* mapped = ub_->Map();
    if (!mapped) return;

    // 4 objects at different positions with different colors.
    ObjectData objs[4] = {};

    // Red, top-left.
    objs[0].tintColor[0] = 1.0f; objs[0].tintColor[1] = 0.2f; objs[0].tintColor[2] = 0.2f; objs[0].tintColor[3] = 1.0f;
    objs[0].offsetScale[0] = -0.45f; objs[0].offsetScale[1] = 0.45f; objs[0].offsetScale[2] = 0.4f; objs[0].offsetScale[3] = 0.4f;

    // Green, top-right.
    objs[1].tintColor[0] = 0.2f; objs[1].tintColor[1] = 1.0f; objs[1].tintColor[2] = 0.2f; objs[1].tintColor[3] = 1.0f;
    objs[1].offsetScale[0] = 0.45f; objs[1].offsetScale[1] = 0.45f; objs[1].offsetScale[2] = 0.4f; objs[1].offsetScale[3] = 0.4f;

    // Blue, bottom-left.
    objs[2].tintColor[0] = 0.2f; objs[2].tintColor[1] = 0.3f; objs[2].tintColor[2] = 1.0f; objs[2].tintColor[3] = 1.0f;
    objs[2].offsetScale[0] = -0.45f; objs[2].offsetScale[1] = -0.45f; objs[2].offsetScale[2] = 0.4f; objs[2].offsetScale[3] = 0.4f;

    // Yellow, bottom-right.
    objs[3].tintColor[0] = 1.0f; objs[3].tintColor[1] = 1.0f; objs[3].tintColor[2] = 0.2f; objs[3].tintColor[3] = 1.0f;
    objs[3].offsetScale[0] = 0.45f; objs[3].offsetScale[1] = -0.45f; objs[3].offsetScale[2] = 0.4f; objs[3].offsetScale[3] = 0.4f;

    std::memcpy(mapped, objs, sizeof(objs));
    ub_->Unmap();
}

void DynamicOffsetSample::onRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetPipeline(pipeline_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);

    // Animate blend constant: pulsing between full visibility and half.
    f32 pulse = 0.5f + 0.5f * std::sin(totalTime_ * 2.0f);
    rp->SetBlendConstant(pulse, pulse, pulse, 1.0f);

    // Draw 4 objects, each at a different dynamic offset.
    for (u32 i = 0; i < 4; i++) {
        u32 off[1] = { i * 256 };
        rp->SetBindGroup(0, bg_, Span<const u32>(off, 1));
        rp->DrawIndexed(6);
    }

    rp->End();
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void DynamicOffsetSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_);
    if (pl_) device_->DestroyPipelineLayout(pl_);
    if (bg_) device_->DestroyBindGroup(bg_);
    if (bgl_) device_->DestroyBindGroupLayout(bgl_);
    if (ub_) device_->DestroyBuffer(ub_);
    if (ib_) device_->DestroyBuffer(ib_);
    if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_);
    if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { DynamicOffsetSample app; return app.run(argc, argv); }
