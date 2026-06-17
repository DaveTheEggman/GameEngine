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
    raptor::core::StringView Title() const override { return u"Sample026 - Dynamic Offsets & Blend Constants"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
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

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr, *m_ub = nullptr;
    dr::BindGroupLayout *m_bgl = nullptr;
    dr::BindGroup *m_bg = nullptr;
    dr::PipelineLayout *m_pl = nullptr;
    dr::RenderPipeline *m_pipeline = nullptr;
    dr::CommandPool *m_pool = nullptr;
    dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
};

raptor::core::Status DynamicOffsetSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"DynVS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"DynPS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Unit quad vertices (will be transformed by UBO data).
    static constexpr float verts[] = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f,
    };
    static constexpr raptor::core::u16 indices[] = { 0, 1, 2, 0, 2, 3 };

    dr::BufferDesc vbd{}; vbd.size = sizeof(verts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(indices); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(verts), sizeof(verts)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(indices), sizeof(indices)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    // Uniform buffer: 4 ObjectData structs (256 bytes each = 1024 total).
    dr::BufferDesc ubd{}; ubd.size = 256 * 4; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(ubd, m_ub) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Initialize UBO data.
    updateUBO();

    // Bind group layout with dynamic offset UBO.
    dr::BindGroupLayoutEntry entry = dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment);
    entry.hasDynamicOffset = true;
    dr::BindGroupLayoutEntry entries[1] = { entry };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(entries, 1);
    if (m_device->CreateBindGroupLayout(bgld, m_bgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bind group (bind the whole buffer, dynamic offset selects the slice).
    dr::BindGroupEntry bgEntries[1] = { dr::BindGroupEntry::BufferEntry(m_ub, 0, 256) };
    dr::BindGroupDesc bgd{}; bgd.layout = m_bgl; bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 1);
    if (m_device->CreateBindGroup(bgd, m_bg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout.
    dr::BindGroupLayout* sets[1] = { m_bgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline with blend constant support.
    dr::VertexAttribute attrs[1] = { { dr::VertexFormat::Float32x3, 0, 0 } };
    dr::VertexBufferLayout vbl{}; vbl.stride = 12; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 1);

    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format(); ct.writeMask = dr::ColorWriteMask::All;
    ct.blend = dr::BlendState{
        { dr::BlendFactor::Constant, dr::BlendFactor::OneMinusConstant, dr::BlendOperation::Add },
        { dr::BlendFactor::One,      dr::BlendFactor::Zero,             dr::BlendOperation::Add }
    };

    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void DynamicOffsetSample::updateUBO() {
    void* mapped = m_ub->Map();
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
    m_ub->Unmap();
}

void DynamicOffsetSample::OnRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

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

    // Animate blend constant: pulsing between full visibility and half.
    f32 pulse = 0.5f + 0.5f * std::sin(m_totalTime * 2.0f);
    rp->SetBlendConstant(pulse, pulse, pulse, 1.0f);

    // Draw 4 objects, each at a different dynamic offset.
    for (u32 i = 0; i < 4; i++) {
        u32 off[1] = { i * 256 };
        rp->SetBindGroup(0, m_bg, Span<const u32>(off, 1));
        rp->DrawIndexed(6);
    }

    rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void DynamicOffsetSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline);
    if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_bg) m_device->DestroyBindGroup(m_bg);
    if (m_bgl) m_device->DestroyBindGroupLayout(m_bgl);
    if (m_ub) m_device->DestroyBuffer(m_ub);
    if (m_ib) m_device->DestroyBuffer(m_ib);
    if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps);
    if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { DynamicOffsetSample app; return app.Run(argc, argv); }
