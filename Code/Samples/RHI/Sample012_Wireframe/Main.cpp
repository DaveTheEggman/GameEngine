#include <new>
/// Sample012 — Wireframe. Ported from Sedulous Sample012_Wireframe.
/// Renders a rotating icosahedron in wireframe mode.

#include <cmath>
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

class WireframeSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample012 - Wireframe"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onResize(raptor::core::u32 w, raptor::core::u32 h) override { depthBuf_.recreate(device_, w, h); }
    void onShutdown() override;
private:
    static constexpr const char8_t kShader[] = u8R"(
        cbuffer UBO : register(b0, space0) { row_major float4x4 MVP; };
        struct VSInput { float3 Position : TEXCOORD0; float4 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float4 Color : COLOR0; };
        PSInput VSMain(VSInput i) { PSInput o; o.Position = mul(MVP, float4(i.Position,1)); o.Color = i.Color; return o; }
        float4 PSMain(PSInput i) : SV_TARGET { return i.Color; }
    )";

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule *vs_=nullptr, *ps_=nullptr;
    dr::Buffer *vb_=nullptr, *ib_=nullptr, *ub_=nullptr;
    void* ubMapped_ = nullptr;
    dr::BindGroupLayout* bgl_=nullptr; dr::BindGroup* bg_=nullptr;
    dr::PipelineLayout* pl_=nullptr;
    dr::RenderPipeline *wirePipe_=nullptr;
    dr::CommandPool* pool_=nullptr; dr::Fence* fence_=nullptr;
    raptor::core::u64 fenceVal_ = 0;
    raptor::core::u32 indexCount_ = 0;
    sf::DepthBuffer depthBuf_;
};

raptor::core::Status WireframeSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::f32;
    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"VS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"PS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Icosahedron.
    f32 t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    f32 s = 1.0f / std::sqrt(1.0f + t*t);
    f32 a = s, b = t*s;
    f32 vertData[84] = {
        -a,b,0, 1,.3f,.3f,1,  a,b,0, .3f,1,.3f,1,  -a,-b,0, .3f,.3f,1,1,  a,-b,0, 1,1,.3f,1,
        0,-a,b, 1,.3f,1,1,  0,a,b, .3f,1,1,1,  0,-a,-b, 1,.6f,.3f,1,  0,a,-b, .6f,.3f,1,1,
        b,0,-a, .3f,1,.6f,1,  b,0,a, 1,.6f,.6f,1,  -b,0,-a, .6f,1,.3f,1,  -b,0,a, .6f,.3f,.6f,1,
    };
    raptor::core::u16 idxData[60] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };
    indexCount_ = 60;

    dr::BufferDesc vbd{}; vbd.size = sizeof(vertData); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(idxData); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->CreateBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::TransferBatch* batch = nullptr; graphicsQueue_->CreateTransferBatch(batch);
    batch->WriteBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(vertData), sizeof(vertData)));
    batch->WriteBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(idxData), sizeof(idxData)));
    batch->Submit(); graphicsQueue_->DestroyTransferBatch(batch);

    dr::BufferDesc ubd{}; ubd.size = 256; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->CreateBuffer(ubd, ub_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    ubMapped_ = ub_->Map();

    dr::BindGroupLayoutEntry bglE[1] = { dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex) };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglE, 1);
    if (device_->CreateBindGroupLayout(bgld, bgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupEntry bgE[1] = { dr::BindGroupEntry::BufferEntry(ub_, 0, 64) };
    dr::BindGroupDesc bgd{}; bgd.layout = bgl_; bgd.entries = Span<const dr::BindGroupEntry>(bgE, 1);
    if (device_->CreateBindGroup(bgd, bg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BindGroupLayout* sets[1] = { bgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    depthBuf_.recreate(device_, width_, height_);

    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x4, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 28; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive = { dr::PrimitiveTopology::TriangleList, dr::FrontFace::CCW, dr::CullMode::None, dr::FillMode::Wireframe };
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::LessEqual;
    rpd.depthStencil->depthWriteEnabled = false;
    if (device_->CreateRenderPipeline(rpd, wirePipe_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void WireframeSample::onRender() {
    using raptor::core::f32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);
    Mat4 model = Mat4::RotationY(totalTime_ * 0.8f);
    f32 view[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,3, 0,0,0,1 };
    Mat4 proj = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Mat4 vMat; std::memcpy(vMat.Data(), view, 64);
    Mat4 mvp = proj * (vMat * model);
    std::memcpy(ubMapped_, mvp.Data(), 64);

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->TransitionTexture(depthBuf_.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store; ca.clearValue = dr::ClearColor(0.06f,0.06f,0.1f,1);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthBuf_.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;
    auto* rp = enc->BeginRenderPass(rpd);
    rp->SetPipeline(wirePipe_); rp->SetBindGroup(0, bg_);
    rp->SetViewport(0,0,static_cast<f32>(width_),static_cast<f32>(height_),0,1);
    rp->SetScissor(0,0,width_,height_);
    rp->SetVertexBuffer(0, vb_, 0); rp->SetIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->DrawIndexed(indexCount_); rp->End();
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_); pool_->DestroyEncoder(enc);
}

void WireframeSample::onShutdown() {
    depthBuf_.destroy(device_);
    if (fence_) device_->DestroyFence(fence_); if (pool_) device_->DestroyCommandPool(pool_);
    if (wirePipe_) device_->DestroyRenderPipeline(wirePipe_); if (pl_) device_->DestroyPipelineLayout(pl_);
    if (bg_) device_->DestroyBindGroup(bg_); if (bgl_) device_->DestroyBindGroupLayout(bgl_);
    if (ub_) device_->DestroyBuffer(ub_); if (ib_) device_->DestroyBuffer(ib_); if (vb_) device_->DestroyBuffer(vb_);
    if (ps_) device_->DestroyShaderModule(ps_); if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { WireframeSample app; return app.run(argc, argv); }
