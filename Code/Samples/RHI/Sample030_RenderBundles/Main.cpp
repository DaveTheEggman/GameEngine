#include <new>
/// Sample030 — Render Bundles. Records the triangle's draw commands into a render bundle
/// (a reusable, off-thread-recordable command sequence) and replays it into the frame's render
/// pass via ExecuteBundles. The pass is begun with RenderPassContents::SecondaryCommandBuffers.
/// Exercises the RHI render-bundle path (Vulkan secondary command buffers / DX12 bundles).

#include <cstdio>
#include <cstdint>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class RenderBundlesSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView Title() const override { return u8"Sample030 - Render Bundles"; }

protected:
    raptor::core::Status OnInit() override;
    void          OnRender() override;
    void          OnShutdown() override;

private:
    static constexpr const char8_t kShaderSource[] = u8R"(
        struct VSInput { float3 Position : TEXCOORD0; float3 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float3 Color : TEXCOORD0; };
        PSInput VSMain(VSInput input) {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.Color = input.Color;
            return output;
        }
        float4 PSMain(PSInput input) : SV_TARGET { return float4(input.Color, 1.0); }
    )";

    static constexpr float kVertexData[] = {
         0.0f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,
    };

    ds::Compiler*        m_compiler  = nullptr;
    dr::Buffer*          m_vertexBuf = nullptr;
    dr::ShaderModule*    m_vs        = nullptr;
    dr::ShaderModule*    m_ps        = nullptr;
    dr::BindGroupLayout* m_bgl       = nullptr;
    dr::PipelineLayout*  m_pl        = nullptr;
    dr::RenderPipeline*  m_pipeline  = nullptr;
    dr::CommandPool*     m_pool      = nullptr;
    dr::Fence*           m_fence     = nullptr;
    raptor::core::u64    m_fenceVal  = 0;
};

raptor::core::Status RenderBundlesSample::OnInit() {
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShaderSource, ds::ShaderStage::Vertex,   u8"VSMain", u8"BundleVS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShaderSource, ds::ShaderStage::Fragment, u8"PSMain", u8"BundlePS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BufferDesc bd{}; bd.size = sizeof(kVertexData); bd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst;
    bd.memory = dr::MemoryLocation::GpuOnly; bd.label = u8"BundleVB";
    if (m_device->CreateBuffer(bd, m_vertexBuf) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TransferBatch* batch = nullptr;
    m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vertexBuf, 0, raptor::core::Span<const raptor::core::u8>(reinterpret_cast<const raptor::core::u8*>(kVertexData), sizeof(kVertexData)));
    batch->Submit();
    m_graphicsQueue->DestroyTransferBatch(batch);

    dr::BindGroupLayoutDesc bglDesc{}; bglDesc.label = u8"EmptyBGL";
    if (m_device->CreateBindGroupLayout(bglDesc, m_bgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::PipelineLayoutDesc pld{};
    dr::BindGroupLayout* sets[1] = { m_bgl };
    pld.bindGroupLayouts = raptor::core::Span<dr::BindGroupLayout* const>(sets, 1);
    pld.label = u8"BundlePL";
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::VertexAttribute attrs[2] = { { dr::VertexFormat::Float32x3, 0, 0 }, { dr::VertexFormat::Float32x3, 12, 1 } };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = raptor::core::Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format(); ct.writeMask = dr::ColorWriteMask::All;

    dr::RenderPipelineDesc rpd{};
    rpd.layout   = m_pl;
    rpd.vertex.shader = { m_vs, u8"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = raptor::core::Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{};
    rpd.fragment->shader = { m_ps, u8"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = raptor::core::Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
    rpd.label = u8"BundlePipeline";
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void RenderBundlesSample::OnRender() {
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    // Record the triangle's draw into a reusable bundle (could be done off-thread).
    dr::RenderBundleDesc bdesc{};
    bdesc.colorFormats[0]   = m_swapChain->Format();
    bdesc.colorFormatCount  = 1;
    bdesc.width             = m_width;
    bdesc.height            = m_height;
    bdesc.label             = u8"TriangleBundle";
    dr::RenderBundleEncoder* be = enc->CreateRenderBundleEncoder(bdesc);
    dr::RenderBundle* bundle = nullptr;
    if (be) {
        be->SetPipeline(m_pipeline);
        be->SetVertexBuffer(0, m_vertexBuf, 0);
        be->Draw(3);
        bundle = be->Finish();
    }

    dr::ColorAttachment ca{};
    ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.1f, 0.1f, 0.15f, 1.0f);

    dr::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);
    rpd.contents = dr::RenderPassContents::SecondaryCommandBuffers;   // pass body is supplied by bundles

    auto* rp = enc->BeginRenderPass(rpd);
    // Viewport/scissor must be set on the parent pass — DX12 bundles inherit these.
    rp->SetViewport(0, 0, static_cast<raptor::core::f32>(m_width), static_cast<raptor::core::f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    if (bundle) {
        dr::RenderBundle* bundles[1] = { bundle };
        rp->ExecuteBundles(raptor::core::Span<dr::RenderBundle* const>(bundles, 1));
    }
    rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish();
    m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(raptor::core::Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);

    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void RenderBundlesSample::OnShutdown() {
    if (m_fence)     m_device->DestroyFence(m_fence);
    if (m_pool)      m_device->DestroyCommandPool(m_pool);
    if (m_pipeline)  m_device->DestroyRenderPipeline(m_pipeline);
    if (m_pl)        m_device->DestroyPipelineLayout(m_pl);
    if (m_bgl)       m_device->DestroyBindGroupLayout(m_bgl);
    if (m_ps)        m_device->DestroyShaderModule(m_ps);
    if (m_vs)        m_device->DestroyShaderModule(m_vs);
    if (m_vertexBuf) m_device->DestroyBuffer(m_vertexBuf);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { RenderBundlesSample app; return app.Run(argc, argv); }
