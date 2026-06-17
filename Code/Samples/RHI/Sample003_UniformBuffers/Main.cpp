#include <new>
/// Sample003 — Rotating Cube with Uniform Buffers + Push Constants.
/// Ported from Sedulous Sample003_UniformBuffers.

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

class UniformBufferSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample003 - Rotating Cube (Uniform Buffers)"; }

protected:
    raptor::core::Status onInit() override;
    void          onRender() override;
    void          onResize(raptor::core::u32 w, raptor::core::u32 h) override { depthBuf_.recreate(device_, w, h); }
    void          onShutdown() override;

private:
    static constexpr const char8_t kShaderSource[] = u8R"(
        cbuffer UBO : register(b0, space0) { row_major float4x4 MVP; };
        struct PushData { float4 Tint; };
        [[vk::push_constant]] ConstantBuffer<PushData> gPush : register(b0, space1);
        struct VSInput { float3 Position : TEXCOORD0; float3 Color : TEXCOORD1; };
        struct PSInput { float4 Position : SV_POSITION; float3 Color : COLOR0; };
        PSInput VSMain(VSInput input) {
            PSInput o; o.Position = mul(MVP, float4(input.Position, 1.0)); o.Color = input.Color; return o;
        }
        float4 PSMain(PSInput input) : SV_TARGET { return float4(input.Color * gPush.Tint.rgb, 1.0); }
    )";

    static constexpr float kCubeVerts[] = {
        -0.5f,-0.5f,-0.5f, 1,0,0,  0.5f,-0.5f,-0.5f, 0,1,0,  0.5f, 0.5f,-0.5f, 0,0,1,  -0.5f, 0.5f,-0.5f, 1,1,0,
        -0.5f,-0.5f, 0.5f, 1,0,1,  0.5f,-0.5f, 0.5f, 0,1,1,  0.5f, 0.5f, 0.5f, 1,1,1,  -0.5f, 0.5f, 0.5f, .5f,.5f,.5f,
    };
    static constexpr raptor::core::u16 kCubeIdx[] = {
        0,2,1, 0,3,2,  4,5,6, 4,6,7,  4,7,3, 4,3,0,  1,2,6, 1,6,5,  3,7,6, 3,6,2,  4,0,1, 4,1,5,
    };

    ds::Compiler* compiler_ = nullptr;
    dr::Buffer *vb_ = nullptr, *ib_ = nullptr, *ub_ = nullptr;
    dr::ShaderModule *vs_ = nullptr, *ps_ = nullptr;
    dr::BindGroupLayout* bgl_ = nullptr;
    dr::BindGroup* bg_ = nullptr;
    dr::PipelineLayout* pl_ = nullptr;
    dr::RenderPipeline* pipeline_ = nullptr;
    dr::CommandPool* pool_ = nullptr;
    dr::Fence* fence_ = nullptr;
    raptor::core::u64 fenceVal_ = 0;
    void* ubMapped_ = nullptr;
    sf::DepthBuffer depthBuf_;
};

raptor::core::Status UniformBufferSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8;

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShaderSource, ds::ShaderStage::Vertex,   u"VSMain", u"CubeVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShaderSource, ds::ShaderStage::Fragment, u"PSMain", u"CubePS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Buffers.
    dr::BufferDesc vbd{}; vbd.size = sizeof(kCubeVerts); vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ibd{}; ibd.size = sizeof(kCubeIdx); ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly;
    if (device_->createBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    dr::BufferDesc ubd{}; ubd.size = 64; ubd.usage = dr::BufferUsage::Uniform; ubd.memory = dr::MemoryLocation::CpuToGpu;
    if (device_->createBuffer(ubd, ub_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    ubMapped_ = ub_->map();

    // Upload.
    dr::TransferBatch* batch = nullptr; graphicsQueue_->createTransferBatch(batch);
    batch->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(kCubeVerts), sizeof(kCubeVerts)));
    batch->writeBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(kCubeIdx), sizeof(kCubeIdx)));
    batch->submit(); graphicsQueue_->destroyTransferBatch(batch);

    // Bind group layout + group.
    dr::BindGroupLayoutEntry bglE[1] = { dr::BindGroupLayoutEntry::uniformBuffer(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment) };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglE, 1);
    if (device_->createBindGroupLayout(bgld, bgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry bgE[1] = { dr::BindGroupEntry::bufferEntry(ub_, 0, 64) };
    dr::BindGroupDesc bgd{}; bgd.layout = bgl_; bgd.entries = Span<const dr::BindGroupEntry>(bgE, 1);
    if (device_->createBindGroup(bgd, bg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout with push constants.
    dr::BindGroupLayout* sets[1] = { bgl_ };
    dr::PushConstantRange pc{ dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, 16 };
    dr::PipelineLayoutDesc pld{};
    pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
    pld.pushConstantRanges = Span<const dr::PushConstantRange>(&pc, 1);
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Depth buffer.
    depthBuf_.recreate(device_, width_, height_);

    // Render pipeline.
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x3, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 24; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->format();

    dr::RenderPipelineDesc rpd{};
    rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive = { dr::PrimitiveTopology::TriangleList, dr::FrontFace::CW, dr::CullMode::Back };
    rpd.depthStencil = dr::DepthStencilState{}; rpd.depthStencil->format = dr::TextureFormat::Depth24PlusStencil8;
    rpd.depthStencil->depthCompare = dr::CompareFunction::Less;
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

void UniformBufferSample::onRender() {
    if (fenceVal_ > 0) fence_->wait(fenceVal_, ~0ull);
    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update MVP.
    raptor::core::f32 aspect = static_cast<raptor::core::f32>(width_) / static_cast<raptor::core::f32>(height_);
    raptor::core::f32 angle = totalTime_ * 1.2f;
    Mat4 model = (Mat4::RotationY(angle) * Mat4::RotationX( angle * 0.7f));
    Mat4 view  = Mat4::LookAtRH(raptor::core::Vec3{0, 1.5f, -3}, raptor::core::Vec3{ 0, 0, 0}, raptor::core::Vec3{ 0, 1, 0});
    Mat4 proj  = Mat4::PerspectiveFovRH(raptor::core::DegreesToRadians(45.0f), aspect, 0.1f, 100.0f);
    Mat4 mvp   = proj * (view * model);
    std::memcpy(ubMapped_, mvp.Data(), 64);

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);
    enc->transitionTexture(depthBuf_.texture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    dr::DepthStencilAttachment dsa{}; dsa.view = depthBuf_.view;
    dsa.depthLoadOp = dr::LoadOp::Clear; dsa.depthStoreOp = dr::StoreOp::Store; dsa.depthClearValue = 1.0f;
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca); rpd.depthStencilAttachment = dsa;

    auto* rp = enc->beginRenderPass(rpd);
    rp->setPipeline(pipeline_);
    rp->setBindGroup(0, bg_);
    raptor::core::f32 pulse = (std::sin(totalTime_ * 2.0f) * 0.3f + 0.7f);
    raptor::core::f32 tint[4] = { pulse, pulse, pulse, 1.0f };
    rp->setPushConstants(dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, 16, tint);
    rp->setViewport(0, 0, static_cast<raptor::core::f32>(width_), static_cast<raptor::core::f32>(height_), 0, 1);
    rp->setScissor(0, 0, width_, height_);
    rp->setVertexBuffer(0, vb_, 0);
    rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
    rp->drawIndexed(36);
    rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(raptor::core::Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void UniformBufferSample::onShutdown() {
    depthBuf_.destroy(device_);
    if (fence_) device_->destroyFence(fence_);
    if (pool_) device_->destroyCommandPool(pool_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_);
    if (pl_) device_->destroyPipelineLayout(pl_);
    if (bg_) device_->destroyBindGroup(bg_);
    if (bgl_) device_->destroyBindGroupLayout(bgl_);
    if (ub_) device_->destroyBuffer(ub_);
    if (ib_) device_->destroyBuffer(ib_);
    if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_);
    if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { UniformBufferSample app; return app.run(argc, argv); }
