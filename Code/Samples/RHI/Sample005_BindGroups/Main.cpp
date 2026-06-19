#include <new>
/// Sample005 — Multiple Bind Groups with Dynamic Offsets.
/// Ported from Sedulous Sample005_BindGroups.
/// 4x4 grid of lit cubes, each with unique color via dynamic offset.

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
using raptor::core::Mat4;

class BindGroupSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::WideStringView Title() const override { return u"Sample005 - Multiple Bind Groups"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnResize(raptor::core::u32 w, raptor::core::u32 h) override { m_depthBuf.Recreate(m_device, w, h); }
    void OnShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        cbuffer GlobalUBO : register(b0, space0) { row_major float4x4 VP; };
        cbuffer ObjectUBO : register(b0, space1) { row_major float4x4 Model; float4 ObjColor; };
        struct VSInput { float3 Position : TEXCOORD0; float3 Normal : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float3 Normal : NORMAL; float4 Color : COLOR; };
        PSInput VSMain(VSInput i) {
            PSInput o;
            float4 wp = mul(float4(i.Position, 1.0), Model);
            o.Position = mul(wp, VP);
            o.Normal = mul(i.Normal, (float3x3)Model);
            o.Color = ObjColor;
            return o;
        }
        float4 PSMain(PSInput i) : SV_TARGET {
            float3 ld = normalize(float3(0.5, 1.0, -0.7));
            float ndotl = max(dot(normalize(i.Normal), ld), 0.0);
            return float4(i.Color.rgb * (0.2 + 0.8 * ndotl), 1.0);
        }
    )";

    static constexpr int kGrid = 4, kObjCount = kGrid * kGrid;
    static constexpr raptor::core::u32 kObjStride = 256; // DX12 CBV alignment

    // Cube with face normals (24 verts, 36 indices).
    struct Vert { float px,py,pz, nx,ny,nz; };
    static constexpr Vert kCubeV[24] = {
        {-.5f,-.5f,-.5f, 0,0,-1},{.5f,-.5f,-.5f, 0,0,-1},{.5f,.5f,-.5f, 0,0,-1},{-.5f,.5f,-.5f, 0,0,-1},
        {.5f,-.5f,.5f, 0,0,1},{-.5f,-.5f,.5f, 0,0,1},{-.5f,.5f,.5f, 0,0,1},{.5f,.5f,.5f, 0,0,1},
        {-.5f,-.5f,.5f,-1,0,0},{-.5f,-.5f,-.5f,-1,0,0},{-.5f,.5f,-.5f,-1,0,0},{-.5f,.5f,.5f,-1,0,0},
        {.5f,-.5f,-.5f,1,0,0},{.5f,-.5f,.5f,1,0,0},{.5f,.5f,.5f,1,0,0},{.5f,.5f,-.5f,1,0,0},
        {-.5f,.5f,-.5f,0,1,0},{.5f,.5f,-.5f,0,1,0},{.5f,.5f,.5f,0,1,0},{-.5f,.5f,.5f,0,1,0},
        {-.5f,-.5f,.5f,0,-1,0},{.5f,-.5f,.5f,0,-1,0},{.5f,-.5f,-.5f,0,-1,0},{-.5f,-.5f,-.5f,0,-1,0},
    };
    static constexpr raptor::core::u16 kCubeI[36] = {
        0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11, 12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23
    };

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule *m_vs = nullptr, *m_ps = nullptr;
    dr::Buffer *m_vb = nullptr, *m_ib = nullptr, *m_globalUbo = nullptr, *m_objUbo = nullptr;
    void *m_globalMapped = nullptr, *m_objMapped = nullptr;
    dr::BindGroupLayout *m_globalBgl = nullptr, *m_objBgl = nullptr;
    dr::BindGroup *m_globalBg = nullptr, *m_objBg = nullptr;
    dr::PipelineLayout *m_pl = nullptr;
    dr::RenderPipeline *m_pipeline = nullptr;
    dr::CommandPool *m_pool = nullptr; dr::Fence *m_fence = nullptr;
    raptor::core::u64 m_fenceVal = 0;
    sf::DepthBuffer m_depthBuf;
};

raptor::core::Status BindGroupSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kCubeV); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kCubeI); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; m_graphicsQueue->CreateTransferBatch(batch);
    batch->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(kCubeV), sizeof(kCubeV)));
    batch->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(kCubeI), sizeof(kCubeI)));
    batch->Submit(); m_graphicsQueue->DestroyTransferBatch(batch);

    dr::BufferDesc gbd{}; gbd.size = 256; gbd.usage = dr::BufferUsage::Uniform; gbd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(gbd, m_globalUbo) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    m_globalMapped = m_globalUbo->Map();
    dr::BufferDesc obd{}; obd.size = kObjCount * kObjStride; obd.usage = dr::BufferUsage::Uniform; obd.memory = dr::MemoryLocation::CpuToGpu;
    if (m_device->CreateBuffer(obd, m_objUbo) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    m_objMapped = m_objUbo->Map();

    // Set 0: global VP.
    dr::BindGroupLayoutEntry gE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc gBgld{}; gBgld.entries = Span<const dr::BindGroupLayoutEntry>(gE, 1);
    if (m_device->CreateBindGroupLayout(gBgld, m_globalBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry gBgE[1] = { dr::BindGroupEntry::BufferEntry(m_globalUbo, 0, 64) };
    dr::BindGroupDesc gBgd{}; gBgd.layout = m_globalBgl; gBgd.entries = Span<const dr::BindGroupEntry>(gBgE, 1);
    if (m_device->CreateBindGroup(gBgd, m_globalBg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Set 1: per-object with dynamic offset.
    dr::BindGroupLayoutEntry oE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment) };
    oE[0].hasDynamicOffset = true;
    dr::BindGroupLayoutDesc oBgld{}; oBgld.entries = Span<const dr::BindGroupLayoutEntry>(oE, 1);
    if (m_device->CreateBindGroupLayout(oBgld, m_objBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry oBgE[1] = { dr::BindGroupEntry::BufferEntry(m_objUbo, 0, kObjStride) };
    dr::BindGroupDesc oBgd{}; oBgd.layout = m_objBgl; oBgd.entries = Span<const dr::BindGroupEntry>(oBgE, 1);
    if (m_device->CreateBindGroup(oBgd, m_objBg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout with 2 sets.
    dr::BindGroupLayout* sets[2] = { m_globalBgl, m_objBgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    m_depthBuf.Recreate(m_device, m_width, m_height);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive = { dr::PrimitiveTopology::TriangleList, dr::FrontFace::CW, dr::CullMode::Back };
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void BindGroupSample::OnRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update VP.
    f32 aspect = static_cast<f32>(m_width) / static_cast<f32>(m_height);
    f32 camAngle = m_totalTime * 0.3f, camDist = 8.0f;
    Mat4 view = Mat4::LookAtRH(raptor::core::Vec3{std::sin(camAngle)*camDist, 5.0f, -std::cos(camAngle)*camDist}, raptor::core::Vec3{ 0,0,0}, raptor::core::Vec3{0,1,0});
    Mat4 proj = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Mat4 vp = view * proj;
    std::memcpy(m_globalMapped, vp.Data(), 64);

    // Update per-object.
    static constexpr f32 kColors[kObjCount * 4] = {
        1,.3f,.3f,1, .3f,1,.3f,1, .3f,.3f,1,1, 1,1,.3f,1, 1,.3f,1,1, .3f,1,1,1, 1,.6f,.2f,1, .6f,.2f,1,1,
        .2f,.8f,.6f,1, .8f,.8f,.8f,1, .5f,.3f,.1f,1, .9f,.5f,.7f,1, .4f,.7f,.2f,1, .2f,.4f,.8f,1, .8f,.4f,.4f,1, .6f,.6f,.3f,1
    };
    f32 spacing = 2.0f, half = (kGrid - 1) * spacing * 0.5f;
    for (int r = 0; r < kGrid; ++r) for (int c = 0; c < kGrid; ++c) {
        int idx = r * kGrid + c;
        f32 angle = m_totalTime * (0.5f + idx * 0.1f);
        Mat4 model = Mat4::RotationY(angle);
        model.m[3][0] = c * spacing - half; model.m[3][1] = 0; model.m[3][2] = r * spacing - half;
        auto* dest = static_cast<raptor::core::u8*>(m_objMapped) + idx * kObjStride;
        std::memcpy(dest, model.Data(), 64);
        std::memcpy(dest + 64, &kColors[idx * 4], 16);
    }

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(m_depthBuf.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1);
    dr::DepthStencilAttachment dsa{}; dsa.view = m_depthBuf.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(m_pipeline);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0, 1);
    rp->SetScissor(0, 0, m_width, m_height);
    rp->SetVertexBuffer(0, m_vb, 0);
    rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
    rp->SetBindGroup(0, m_globalBg);
    for (int i = 0; i < kObjCount; ++i) {
        u32 dynOff = static_cast<u32>(i * kObjStride);
        rp->SetBindGroup(1, m_objBg, Span<const u32>(&dynOff, 1));
        rp->DrawIndexed(36);
    }
    rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue); m_pool->DestroyEncoder(enc);
}

void BindGroupSample::OnShutdown() {
    m_depthBuf.Destroy(m_device);
    if (m_fence) m_device->DestroyFence(m_fence); if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline); if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_objBg) m_device->DestroyBindGroup(m_objBg); if (m_objBgl) m_device->DestroyBindGroupLayout(m_objBgl);
    if (m_globalBg) m_device->DestroyBindGroup(m_globalBg); if (m_globalBgl) m_device->DestroyBindGroupLayout(m_globalBgl);
    if (m_objUbo) m_device->DestroyBuffer(m_objUbo); if (m_globalUbo) m_device->DestroyBuffer(m_globalUbo);
    if (m_ib) m_device->DestroyBuffer(m_ib); if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps); if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { BindGroupSample app; return app.Run(argc, argv); }
