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
    raptor::core::StringView Title() const override { return u"Sample019 - Batch Upload (Async Transfer)"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
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

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule* m_vs = nullptr;
    dr::ShaderModule* m_ps = nullptr;

    dr::Buffer* m_vb = nullptr;
    dr::Buffer* m_ib = nullptr;
    dr::Texture* m_tex = nullptr;
    dr::TextureView* m_texView = nullptr;
    dr::Sampler* m_sampler = nullptr;

    dr::Buffer* m_transformBuf = nullptr;
    void* m_transformMapped = nullptr;

    dr::BindGroupLayout* m_bgl = nullptr;
    dr::BindGroup* m_bg = nullptr;
    dr::PipelineLayout* m_pl = nullptr;
    dr::RenderPipeline* m_pipeline = nullptr;
    dr::CommandPool* m_pool = nullptr;
    dr::Fence* m_frameFence = nullptr;
    raptor::core::u64 m_frameFenceVal = 0;

    // Upload tracking
    dr::Fence* m_uploadFence = nullptr;
    raptor::core::u64 m_uploadFenceVal = 0;
    bool m_uploadComplete = false;
    float m_uploadStartTime = 0.0f;
};

raptor::core::Status BatchUploadSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"BatchVS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"BatchPS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex buffer: 4 vertices x (pos3 + uv2) x 4 = 80 bytes
    dr::BufferDesc vbd{}; vbd.size = 80; vbd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst; vbd.memory = dr::MemoryLocation::GpuOnly; vbd.label = u"BatchVB";
    if (m_device->CreateBuffer(vbd, m_vb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Index buffer: 6 uint16 = 12 bytes
    dr::BufferDesc ibd{}; ibd.size = 12; ibd.usage = dr::BufferUsage::Index | dr::BufferUsage::CopyDst; ibd.memory = dr::MemoryLocation::GpuOnly; ibd.label = u"BatchIB";
    if (m_device->CreateBuffer(ibd, m_ib) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Texture
    dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm; td.width = kTexSize; td.height = kTexSize;
    td.mipLevelCount = 1; td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst; td.label = u"BatchTex";
    if (m_device->CreateTexture(td, m_tex) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm; tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
    if (m_device->CreateTextureView(m_tex, tvd, m_texView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Linear; sd.magFilter = dr::FilterMode::Linear;
    sd.addressU = dr::AddressMode::Repeat; sd.addressV = dr::AddressMode::Repeat; sd.label = u"BatchSampler";
    if (m_device->CreateSampler(sd, m_sampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Transform UBO
    dr::BufferDesc tbd{}; tbd.size = 16; tbd.usage = dr::BufferUsage::Uniform; tbd.memory = dr::MemoryLocation::CpuToGpu; tbd.label = u"BatchTransform";
    if (m_device->CreateBuffer(tbd, m_transformBuf) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    m_transformMapped = m_transformBuf->Map();

    // Bind group layout: UBO + texture + sampler
    dr::BindGroupLayoutEntry bglEntries[3] = {
        dr::BindGroupLayoutEntry::UniformBuffer(0, dr::ShaderStage::Vertex),
        dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment),
        dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment),
    };
    dr::BindGroupLayoutDesc bgld{}; bgld.entries = Span<const dr::BindGroupLayoutEntry>(bglEntries, 3); bgld.label = u"BatchBGL";
    if (m_device->CreateBindGroupLayout(bgld, m_bgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry bgEntries[3] = {
        dr::BindGroupEntry::BufferEntry(m_transformBuf, 0, 16),
        dr::BindGroupEntry::TextureEntry(m_texView),
        dr::BindGroupEntry::SamplerEntry(m_sampler),
    };
    dr::BindGroupDesc bgd{}; bgd.layout = m_bgl; bgd.entries = Span<const dr::BindGroupEntry>(bgEntries, 3); bgd.label = u"BatchBG";
    if (m_device->CreateBindGroup(bgd, m_bg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout
    dr::BindGroupLayout* bgls[1] = { m_bgl };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(bgls, 1); pld.label = u"BatchPL";
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline
    dr::VertexAttribute attrs[2] = { {dr::VertexFormat::Float32x3, 0, 0}, {dr::VertexFormat::Float32x2, 12, 1} };
    dr::VertexBufferLayout vbl{}; vbl.stride = 20; vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
    rpd.label = u"BatchPipeline";
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_frameFence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Upload fence
    if (m_device->CreateFence(0, m_uploadFence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // === Batch upload: VB + IB + texture in one submission ===
    if (doBatchUpload() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status BatchUploadSample::doBatchUpload() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    m_uploadStartTime = m_totalTime;

    dr::TransferBatch* transfer = nullptr;
    if (m_graphicsQueue->CreateTransferBatch(transfer) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Vertex data: quad
    float verts[20] = {
        -0.6f,  0.6f, 0.0f,   0.0f, 0.0f,
         0.6f,  0.6f, 0.0f,   1.0f, 0.0f,
         0.6f, -0.6f, 0.0f,   1.0f, 1.0f,
        -0.6f, -0.6f, 0.0f,   0.0f, 1.0f,
    };
    transfer->WriteBuffer(m_vb, 0, Span<const u8>(reinterpret_cast<const u8*>(verts), 80));

    // Index data
    raptor::core::u16 indices[6] = { 0, 1, 2, 0, 2, 3 };
    transfer->WriteBuffer(m_ib, 0, Span<const u8>(reinterpret_cast<const u8*>(indices), 12));

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
    transfer->WriteTexture(m_tex, Span<const u8>(pixels, texBytes), layout, dr::Extent3D{kTexSize, kTexSize, 1});

    delete[] pixels;

    // Async submit - signals fence when GPU transfer completes
    m_uploadFenceVal = 1;
    if (transfer->SubmitAsync(m_uploadFence, m_uploadFenceVal) != raptor::core::ErrorCode::Ok) {
        m_graphicsQueue->DestroyTransferBatch(transfer);
        return raptor::core::ErrorCode::Unknown;
    }

    std::printf("Batch upload submitted asynchronously (VB: 80B, IB: 12B, Tex: %uB)\n", texBytes);
    m_graphicsQueue->DestroyTransferBatch(transfer);
    return raptor::core::ErrorCode::Ok;
}

void BatchUploadSample::OnRender() {
    using raptor::core::f32, raptor::core::Span;

    if (m_frameFenceVal > 0) m_frameFence->Wait(m_frameFenceVal, ~0ull);

    // Check if async upload has completed
    if (!m_uploadComplete) {
        if (m_uploadFence->CompletedValue() >= m_uploadFenceVal) {
            m_uploadComplete = true;
            std::printf("Batch upload completed! Rendering enabled.\n");
        }
    }

    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    // Update transform
    float transform[4] = { m_totalTime, 0, 0, 0 };
    std::memcpy(m_transformMapped, transform, 16);

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    if (m_uploadComplete) {
        rp->SetPipeline(m_pipeline);
        rp->SetBindGroup(0, m_bg);
        rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f);
        rp->SetScissor(0, 0, m_width, m_height);
        rp->SetVertexBuffer(0, m_vb, 0);
        rp->SetIndexBuffer(m_ib, dr::IndexFormat::UInt16, 0);
        rp->DrawIndexed(6);
    }

    rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish(); m_frameFenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_frameFence, m_frameFenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void BatchUploadSample::OnShutdown() {
    if (m_transformBuf && m_transformMapped) m_transformBuf->Unmap();

    if (m_uploadFence) m_device->DestroyFence(m_uploadFence);
    if (m_frameFence) m_device->DestroyFence(m_frameFence);
    if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline);
    if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_bg) m_device->DestroyBindGroup(m_bg);
    if (m_bgl) m_device->DestroyBindGroupLayout(m_bgl);
    if (m_sampler) m_device->DestroySampler(m_sampler);
    if (m_texView) m_device->DestroyTextureView(m_texView);
    if (m_tex) m_device->DestroyTexture(m_tex);
    if (m_transformBuf) m_device->DestroyBuffer(m_transformBuf);
    if (m_ib) m_device->DestroyBuffer(m_ib);
    if (m_vb) m_device->DestroyBuffer(m_vb);
    if (m_ps) m_device->DestroyShaderModule(m_ps);
    if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { BatchUploadSample app; return app.Run(argc, argv); }
