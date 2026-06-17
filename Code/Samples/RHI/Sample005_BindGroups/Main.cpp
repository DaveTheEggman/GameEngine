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
    raptor::core::StringView title() const override { return u"Sample005 - Multiple Bind Groups"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { depthBuf_.recreate(device_, w, h); }
    void onShutdown() override;
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

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *globalUbo_ = nullptr, *objUbo_ = nullptr;
    void *globalMapped_ = nullptr, *objMapped_ = nullptr;
    dr::BindGroupLayout *globalBgl_ = nullptr, *objBgl_ = nullptr;
    dr::BindGroup *globalBg_ = nullptr, *objBg_ = nullptr;
    dr::PipelineLayout *pl_ = nullptr;
    dr::RenderPipeline *pipeline_ = nullptr;
    dr::CommandPool *pool_ = nullptr; dr::Fence *fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    sf::DepthBuffer depthBuf_;
};

raptor::core::Status BindGroupSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kCubeV); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kCubeI); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kCubeV), sizeof(kCubeV)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kCubeI), sizeof(kCubeI)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    dr::BufferDesc gbd{}; gbd.size = 256; gbd.usage = dr::BufferUsage::Uniform; gbd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(gbd, globalUbo_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    globalMapped_ = globalUbo_->Map();
    dr::BufferDesc obd{}; obd.size = kObjCount * kObjStride; obd.usage = dr::BufferUsage::Uniform; obd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(obd, objUbo_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    objMapped_ = objUbo_->Map();

    // Set 0: global VP.
    dr::BindGroupLayoutEntry gE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc gBgld{}; gBgld.entries = Span<const dr::BindGroupLayoutEntry>(gE, 1);
    if (device_->CreateBindGroupLayout(gBgld, globalBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry gBgE[1] = { dr::BindGroupEntry::BufferEntry(globalUbo_, 0, 64) };
    dr::BindGroupDesc gBgd{}; gBgd.layout = globalBgl_; gBgd.entries = Span<const dr::BindGroupEntry>(gBgE, 1);
    if (device_->CreateBindGroup(gBgd, globalBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Set 1: per-object with dynamic offset.
    dr::BindGroupLayoutEntry oE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment) };
    oE[0].hasDynamicOffset = true;
    dr::BindGroupLayoutDesc oBgld{}; oBgld.entries = Span<const dr::BindGroupLayoutEntry>(oE, 1);
    if (device_->CreateBindGroupLayout(oBgld, objBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry oBgE[1] = { dr::BindGroupEntry::BufferEntry(objUbo_, 0, kObjStride) };
    dr::BindGroupDesc oBgd{}; oBgd.layout = objBgl_; oBgd.entries = Span<const dr::BindGroupEntry>(oBgE, 1);
    if (device_->CreateBindGroup(oBgd, objBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout with 2 sets.
    dr::BindGroupLayout* sets[2] = { globalBgl_, objBgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    depthBuf_.recreate(device_, width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive = { dr::PrimitiveTopology::TriangleList, dr::FrontFace::CW, dr::CullMode::Back };
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void BindGroupSample::onRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update VP.
    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);
    f32 camAngle = totalTime_ * 0.3f, camDist = 8.0f;
    Mat4 view = Mat4::LookAtRH(raptor::core::Vec3{std::sin(camAngle)*camDist, 5.0f, -std::cos(camAngle)*camDist}, raptor::core::Vec3{ 0,0,0}, raptor::core::Vec3{0,1,0});
    Mat4 proj = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Mat4 vp = view * proj;
    std::memcpy(globalMapped_, vp.Data(), 64);

    // Update per-object.
    static constexpr f32 kColors[kObjCount * 4] = {
        1,.3f,.3f,1, .3f,1,.3f,1, .3f,.3f,1,1, 1,1,.3f,1, 1,.3f,1,1, .3f,1,1,1, 1,.6f,.2f,1, .6f,.2f,1,1,
        .2f,.8f,.6f,1, .8f,.8f,.8f,1, .5f,.3f,.1f,1, .9f,.5f,.7f,1, .4f,.7f,.2f,1, .2f,.4f,.8f,1, .8f,.4f,.4f,1, .6f,.6f,.3f,1
    };
    f32 spacing = 2.0f, half = (kGrid - 1) * spacing * 0.5f;
    for (int r = 0; r < kGrid; ++r) for (int c = 0; c < kGrid; ++c) {
        int idx = r * kGrid + c;
        f32 angle = totalTime_ * (0.5f + idx * 0.1f);
        Mat4 model = Mat4::RotationY(angle);
        model.m[3][0] = c * spacing - half; model.m[3][1] = 0; model.m[3][2] = r * spacing - half;
        auto* dest = static_cast<raptor::core::u8*>(objMapped_) + idx * kObjStride;
        std::memcpy(dest, model.Data(), 64);
        std::memcpy(dest + 64, &kColors[idx * 4], 16);
    }

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(depthBuf_.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.08f, 0.08f, 0.12f, 1);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthBuf_.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(pipeline_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0, 1);
    rp->SetScissor(0, 0, width_, height_);
    rp->SetVertexBuffer(0, vb_, 0);
    rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->SetBindGroup(0, globalBg_);
    for (int i = 0; i < kObjCount; ++i) {
        u32 dynOff = static_cast<u32>(i * kObjStride);
        rp->SetBindGroup(1, objBg_, Span<const u32>(&dynOff, 1));
        rp->DrawIndexed(36);
    }
    rp->End();
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_); pool_->DestroyEncoder(enc);
}

void BindGroupSample::onShutdown() {
    depthBuf_.destroy(device_);
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_); if (pl_) device_->DestroyPipelineLayout(pl_);
    if (objBg_) device_->DestroyBindGroup(objBg_); if (objBgl_) device_->DestroyBindGroupLayout(objBgl_);
    if (globalBg_) device_->DestroyBindGroup(globalBg_); if (globalBgl_) device_->DestroyBindGroupLayout(globalBgl_);
    if (objUbo_) device_->DestroyBuffer(objUbo_); if (globalUbo_) device_->DestroyBuffer(globalUbo_);
    if (ib_) device_->DestroyBuffer(ib_); if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { BindGroupSample app; return app.run(argc, argv); }
