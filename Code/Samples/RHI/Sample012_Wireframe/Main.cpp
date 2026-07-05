#include <new>
/// Sample012 — Wireframe. Ported from Sedulous Sample012_Wireframe.
/// Renders a rotating icosahedron in wireframe mode.

#include <cmath>
#include <cstring>

import draconic.core;
import draconic.rhi;
import draconic.shaders;
import draconic.samples.framework;
import draconic.rhi.vk;

namespace sf = draconic::samples::framework;
namespace dr = draconic::rhi;
namespace ds = draconic::shaders;
using draconic::core::Matrix4;

class WireframeSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    draconic::core::StringView Title() const override { return u8"Sample012 - Wireframe"; }
protected:
    draconic::core::Status OnInit() override;
    void OnRender() override;
    void OnResize(draconic::core::u32 w, draconic::core::u32 h) override { m_depthBuf.Recreate(m_device, w, h); }
    void OnShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        cbuffer UBO : register(b0, space0) { row_major float4x4 MVP; };
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = mul(float4(i.Position,1), MVP); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return i.Color; }
    )";

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs=nullptr, *m_ps=nullptr;
    dr::Buffer *m_vb=nullptr, *m_ib=nullptr, *m_ub=nullptr;
    void* m_ubMapped = nullptr;
    dr::BindGroupLayout* m_bgl=nullptr; dr::BindGroup* m_bg=nullptr;
    dr::PipelineLayout* m_pl=nullptr;
    dr::RenderPipeline *m_wirePipe=nullptr;
    dr::CommandPool* m_pool=nullptr; dr::Fence* m_fence=nullptr;
    draconic::core::u64 m_fenceVal = 0;
    draconic::core::u32 m_indexCount = 0;
    sf::DepthBuffer m_depthBuf;
};

draconic::core::Status WireframeSample::OnInit() {
    using draconic::core::Status, draconic::core::Span, draconic::core::u8, draconic::core::f32;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"VS", m_vs) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u8"PSMain", u8"PS", m_ps) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    // Icosahedron.
    f32 t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    f32 s = 1.0f / std::sqrt(1.0f + t*t);
    f32 a = s, b = t*s;
    f32 vertData[84] = {
        -a,b,0, 1,.3f,.3f,1,  a,b,0, .3f,1,.3f,1,  -a,-b,0, .3f,.3f,1,1,  a,-b,0, 1,1,.3f,1,
        0,-a,b, 1,.3f,1,1,  0,a,b, .3f,1,1,1,  0,-a,-b, 1,.6f,.3f,1,  0,a,-b, .6f,.3f,1,1,
        b,0,-a, .3f,1,.6f,1,  b,0,a, 1,.6f,.6f,1,  -b,0,-a, .6f,1,.3f,1,  -b,0,a, .6f,.3f,.6f,1,
    };
    draconic::core::u16 idxData[60] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };
    m_indexCount = 60;

    dr::BufferDesc vbd{}; vbd.size = sizeof(vertData); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(idxData); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(vertData), sizeof(vertData)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(idxData), sizeof(idxData)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::BufferDesc ubd{}; ubd.size = 256; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(ubd, m_ub) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    m_ubMapped = m_ub->Map();

    dr::BindGroupLayoutEntry bglE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglE, 1);
    if (m_device->CreateBindGroupLayout(bgld, m_bgl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BindGroupEntry bgE[1] = { dr::BindGroupEntry::BufferEntry(m_ub, 0, 64) };
    dr::BindGroupDesc bgd{}; bgd.layout = m_bgl; bgd.entries = Span<const dr::BindGroupEntry>(bgE, 1);
    if (m_device->CreateBindGroup(bgd, m_bg) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    dr::BindGroupLayout* sets[1] = { m_bgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    if (m_device->CreatePipelineLayout(pld, m_pl) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    m_depthBuf.Recreate(m_device, m_width, m_height);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u8"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u8"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive = { dr::PrimitiveTopology::TriangleList, dr::FrontFace::CCW, dr::CullMode::None, dr::FillMode::Wireframe };
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::LessEqual;
    rpd.depthStencil->depthWriteEnabled = false;
    if (m_device->CreateRenderPipeline(rpd, m_wirePipe) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != draconic::core::ErrorCode::Ok) return draconic::core::ErrorCode::Unknown;
    return draconic::core::ErrorCode::Ok;
}

void WireframeSample::OnRender() {
    using draconic::core::f32, draconic::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != draconic::core::ErrorCode::Ok) return;
    f32 aspect = static_cast<f32>(m_width) / static_cast<f32>(m_height);
    Matrix4 model = Matrix4::RotationY(m_totalTime * 0.8f);
    // Row-vector view: identity rotation, camera 3 units along +Z (RH: looking toward -Z).
    // Translation in row 3: m[3][2] = 3.
    f32 view[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,3,1 };
    Matrix4 proj = Matrix4::PerspectiveFovRH(draconic::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Matrix4 vMat; std::memcpy(vMat.Data(), view, 64);
    Matrix4 mvp = model * vMat * proj;
    std::memcpy(m_ubMapped, mvp.Data(), 64);

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != draconic::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(m_depthBuf.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.06f,0.06f,0.1f,1);
    dr::DepthStencilAttachment dsa{}; dsa.view = m_depthBuf.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(m_wirePipe); rp->SetBindGroup(0, m_bg);
    rp->SetViewport(0,0,static_cast<f32>(m_width),static_cast<f32>(m_height),0,1);
    rp->SetScissor(0,0,m_width,m_height);
    rp->SetVertexBuffer(0, m_vb, 0); rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(m_indexCount); rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue); m_pool->DestroyEncoder(enc);
}

void WireframeSample::OnShutdown() {
    m_depthBuf.Destroy(m_device);
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_wirePipe) m_device->DestroyRenderPipeline(m_wirePipe); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_bg) m_device->DestroyBindGroup(m_bg); if (m_bgl) m_device->DestroyBindGroupLayout(m_bgl);
    if (m_ub) m_device->DestroyBuffer(m_ub); if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { WireframeSample app; return app.Run(argc, argv); }
