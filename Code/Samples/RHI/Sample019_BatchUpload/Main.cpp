#include <new>
/// Sample019 -- Batch Upload (Async Transfer). Ported from Sedulous Sample019_BatchUpload.
/// Demonstrates batched GPU uploads using TransferBatch with async fence signaling.
/// Uploads a vertex buffer, index buffer, and a procedural texture in a single
/// batched transfer with submitAsync, then renders a textured quad once the
/// upload fence signals completion.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

import raptor.core;
import raptor.rhi;
import raptor.shaders;
import raptor.samples.framework;
import raptor.rhi.vk;

namespace sf = raptor::samples::framework;
namespace dr = raptor::rhi;
namespace ds = raptor::shaders;

class BatchUploadSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample019 - Batch Upload (Async Transfer)"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    raptor::core::Status doBatchUpload();

    static constexpr const char8_t kShader[] = u8R"(
        Texture2D gTexture : register(t0, space0);
        SamplerState gSampler : register(s0, space0);

        struct VSInput
        {
            float3 Position : TEXCOORD0;
            float2 TexCoord : TEXCOORD1;
        };

        struct PSInput
        {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };

        cbuffer Transform : register(b0, space0)
        {
            float Time;
            float Pad0;
            float Pad1;
            float Pad2;
        };

        PSInput VSMain(VSInput input)
        {
            PSInput output;
            // Gentle rotation
            float c = cos(Time * 0.5);
            float s = sin(Time * 0.5);
            float3 p = input.Position;
            float x = p.x * c - p.y * s;
            float y = p.x * s + p.y * c;
            output.Position = float4(x, y, p.z, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            return gTexture.Sample(gSampler, input.TexCoord);
        }
    )";

    static constexpr raptor::core::u32 kTexSize = 128;

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule* vs_ = nullptr;
    dr::ShaderModule* ps_ = nullptr;

    dr::Buffer* vb_ = nullptr;
    dr::Buffer* ib_ = nullptr;
    dr::Texture* tex_ = nullptr;
    dr::TextureView* texView_ = nullptr;
    dr::Sampler* sampler_ = nullptr;

    dr::Buffer* transformBuf_ = nullptr;
    void* transformMapped_ = nullptr;

    dr::BindGroupLayout* bgl_ = nullptr;
    dr::BindGroup* bg_ = nullptr;
    dr::PipelineLayout* pl_ = nullptr;
    dr::RenderPipeline* pipeline_ = nullptr;
    dr::CommandPool* pool_ = nullptr;
    dr::Fence* frameFence_ = nullptr;
    raptor::core::u64 frameFenceVal_ = 0;

    // Upload tracking
    dr::Fence* uploadFence_ = nullptr;
    raptor::core::u64 uploadFenceVal_ = 0;
    bool uploadComplete_ = false;
    float uploadStartTime_ = 0.0f;
};

raptor::core::Status BatchUploadSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"BatchVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"BatchPS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex buffer: 4 vertices x (pos3 + uv2) x 4 = 80 bytes
    dr::BufferDesc vbd{}; vbd.size = 80; vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly; vbd.label = u"BatchVB";
    if (device_->createBuffer(vbd, vb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Index buffer: 6 uint16 = 12 bytes
    dr::BufferDesc ibd{}; ibd.size = 12; ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly; ibd.label = u"BatchIB";
    if (device_->createBuffer(ibd, ib_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Texture
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = kTexSize; td.height = kTexSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst; td.label = u"BatchTex";
    if (device_->createTexture(td, tex_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (device_->createTextureView(tex_, tvd, texView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Linear; sd.magFilter = dr::FilterMode::Linear;
    sd.addressU = dr::AddressMode::Repeat; sd.addressV = dr::AddressMode::Repeat; sd.label = u"BatchSampler";
    if (device_->createSampler(sd, sampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Transform UBO
    dr::BufferDesc tbd{}; tbd.size = 16; tbd.usage = dr::BufferUsage::Uniform; tbd.memory = dr::MemoryLocation::CpuToGpu; tbd.label = u"BatchTransform";
    if (device_->createBuffer(tbd, transformBuf_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    transformMapped_ = transformBuf_->map();

    // Bind group layout: UBO + texture + sampler
    dr::BindGroupLayoutEntry bglEntries[3] = {
        dr::BindGroupLayoutEntry::uniformBuffer(0, dr::ShaderStage::Vertex),
        dr::BindGroupLayoutEntry::sampledTexture(0, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::sampler(0, dr::ShaderStage::Fragment),
    };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglEntries, 3); bgld.label = u"BatchBGL";
    if (device_->createBindGroupLayout(bgld, bgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry bgEntries[3] = {
        dr::BindGroupEntry::bufferEntry(transformBuf_, 0, 16),
        dr::BindGroupEntry::textureEntry(texView_),
        dr::BindGroupEntry::samplerEntry(sampler_),
    };
    dr::BindGroupDesc bgd{}; bgd.layout = bgl_; bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 3); bgd.label = u"BatchBG";
    if (device_->createBindGroup(bgd, bg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout
    dr::BindGroupLayout* bgls[1] = { bgl_ };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(bgls, 1); pld.label = u"BatchPL";
    if (device_->createPipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = swapChain_->format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
    rpd.label = u"BatchPipeline";
    if (device_->createRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->createCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->createFence(0, frameFence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Upload fence
    if (device_->createFence(0, uploadFence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // === Batch upload: VB + IB + texture in one submission ===
    if (doBatchUpload() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status BatchUploadSample::doBatchUpload() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    uploadStartTime_ = totalTime_;

    dr::TransferBatch* transfer = nullptr;
    if (graphicsQueue_->createTransferBatch(transfer) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex data: quad
    float verts[20] = {
        -0.6f,  0.6f, 0.0f,   0.0f, 0.0f,
         0.6f,  0.6f, 0.0f,   1.0f, 0.0f,
         0.6f, -0.6f, 0.0f,   1.0f, 1.0f,
        -0.6f, -0.6f, 0.0f,   0.0f, 1.0f,
    };
    transfer->writeBuffer(vb_, 0, Span<const u8>(reinterpret_cast<const u8*>(verts), 80));

    // Index data
    raptor::core::u16 indices[6] = { 0, 1, 2, 0, 2, 3 };
    transfer->writeBuffer(ib_, 0, Span<const u8>(reinterpret_cast<const u8*>(indices), 12));

    // Texture data: procedural mandelbrot-ish pattern
    u32 texBytes = kTexSize * kTexSize * 4;
    auto* pixels = new u8[texBytes];

    for (u32 y = 0; y < kTexSize; y++) {
        for (u32 x = 0; x < kTexSize; x++) {
            float cr = static_cast<float>(x) / static_cast<float>(kTexSize) * 3.0f - 2.0f;
            float ci = static_cast<float>(y) / static_cast<float>(kTexSize) * 2.4f - 1.2f;
            float zr = 0, zi = 0;
            int iter = 0;
            for (iter = 0; iter < 64; iter++) {
                float zr2 = zr * zr - zi * zi + cr;
                float zi2 = 2.0f * zr * zi + ci;
                zr = zr2; zi = zi2;
                if (zr * zr + zi * zi > 4.0f) break;
            }

            u32 off = (y * kTexSize + x) * 4;
            if (iter == 64) {
                pixels[off] = 10; pixels[off + 1] = 10; pixels[off + 2] = 30; pixels[off + 3] = 255;
            } else {
                float t = static_cast<float>(iter) / 64.0f;
                pixels[off]     = static_cast<u8>(t * 200 + 55);
                pixels[off + 1] = static_cast<u8>(t * t * 255);
                pixels[off + 2] = static_cast<u8>(std::sqrt(t) * 255);
                pixels[off + 3] = 255;
            }
        }
    }

    dr::TextureDataLayout layout{}; layout.bytesPerRow = kTexSize * 4; layout.rowsPerImage = kTexSize;
    transfer->writeTexture(tex_, Span<const u8>(pixels, texBytes), layout, dr::Extent3D{kTexSize, kTexSize, 1});

    delete[] pixels;

    // Async submit - signals fence when GPU transfer completes
    uploadFenceVal_ = 1;
    if (transfer->submitAsync(uploadFence_, uploadFenceVal_) != raptor::core::ErrorCode::Ok) {
        graphicsQueue_->destroyTransferBatch(transfer);
        return raptor::core::ErrorCode::Unknown;
    }

    std::printf("Batch upload submitted asynchronously (VB: 80B, IB: 12B, Tex: %uB)\n", texBytes);
    graphicsQueue_->destroyTransferBatch(transfer);
    return raptor::core::ErrorCode::Ok;
}

void BatchUploadSample::onRender() {
    using raptor::core::f32, raptor::core::Span;

    if (frameFenceVal_ > 0) frameFence_->wait(frameFenceVal_, ~0ull);

    // Check if async upload has completed
    if (!uploadComplete_) {
        if (uploadFence_->completedValue() >= uploadFenceVal_) {
            uploadComplete_ = true;
            std::printf("Batch upload completed! Rendering enabled.\n");
        }
    }

    if (swapChain_->acquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update transform
    float transform[4] = { totalTime_, 0, 0, 0 };
    std::memcpy(transformMapped_, transform, 16);

    pool_->reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->createEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->currentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->beginRenderPass(rpd);

    if (uploadComplete_) {
        rp->setPipeline(pipeline_);
        rp->setBindGroup(0, bg_);
        rp->setViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0.0f, 1.0f);
        rp->setScissor(0, 0, width_, height_);
        rp->setVertexBuffer(0, vb_, 0);
        rp->setIndexBuffer(ib_, dr::IndexFormat::UInt16, 0);
        rp->drawIndexed(6);
    }

    rp->end();

    enc->transitionTexture(swapChain_->currentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->finish(); frameFenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->submit(Span<dr::CommandBuffer* const>(cbs, 1), frameFence_, frameFenceVal_);
    swapChain_->present(graphicsQueue_);
    pool_->destroyEncoder(enc);
}

void BatchUploadSample::onShutdown() {
    if (transformBuf_ && transformMapped_) transformBuf_->unmap();

    if (uploadFence_) device_->destroyFence(uploadFence_);
    if (frameFence_) device_->destroyFence(frameFence_);
    if (pool_) device_->destroyCommandPool(pool_);
    if (pipeline_) device_->destroyRenderPipeline(pipeline_);
    if (pl_) device_->destroyPipelineLayout(pl_);
    if (bg_) device_->destroyBindGroup(bg_);
    if (bgl_) device_->destroyBindGroupLayout(bgl_);
    if (sampler_) device_->destroySampler(sampler_);
    if (texView_) device_->destroyTextureView(texView_);
    if (tex_) device_->destroyTexture(tex_);
    if (transformBuf_) device_->destroyBuffer(transformBuf_);
    if (ib_) device_->destroyBuffer(ib_);
    if (vb_) device_->destroyBuffer(vb_);
    if (ps_) device_->destroyShaderModule(ps_);
    if (vs_) device_->destroyShaderModule(vs_);
    if (compiler_) { compiler_->destroy(); delete compiler_; }
}

int main(int argc, char** argv) { BatchUploadSample app; return app.run(argc, argv); }
