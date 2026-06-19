#include <new>
/// Sample027 -- 3D Texture & 1D LUT. Ported from Sedulous Sample027_3DTexture.
/// Demonstrates 3D textures and 1D textures.
/// Generates a 3D noise volume, renders slices animated over time.
/// Uses a 1D gradient LUT for color mapping.

#include <algorithm>
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

class Texture3DSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView Title() const override { return u8"Sample027 - 3D Texture & 1D LUT"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
private:
    raptor::core::Status createVolumeTexture();
    raptor::core::Status createLUTTexture();

    static constexpr const char8_t kShader[] = u8R"(
        Texture3D<float4> gVolume : register(t0, space0);
        Texture1D<float4> gLUT    : register(t1, space0);
        SamplerState gSampler     : register(s0, space0);

        struct PushConstants
        {
            float SliceZ;
            float Time;
            float2 _pad;
        };

        [[vk::push_constant]] ConstantBuffer<PushConstants> pc : register(b0, space1);

        struct PSInput
        {
            float4 Position : SV_POSITION;
            float2 UV       : TEXCOORD0;
        };

        PSInput VSMain(uint vertexID : SV_VertexID)
        {
            PSInput output;
            float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
            output.Position = float4(uv * 2.0 - 1.0, 0.5, 1.0);
            output.UV = uv;
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            // Sample 3D volume at current slice
            float3 uvw = float3(input.UV, pc.SliceZ);
            float density = gVolume.Sample(gSampler, uvw).r;

            // Map density through 1D LUT
            float4 color = gLUT.Sample(gSampler, density);
            return color;
        }
    )";

    struct PushData {
        float sliceZ;
        float time;
        float _pad0;
        float _pad1;
    };

    static constexpr raptor::core::u32 kVolumeSize = 32;
    static constexpr raptor::core::u32 kLUTSize = 64;

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule* m_vs = nullptr;
    dr::ShaderModule* m_ps = nullptr;

    // 3D volume texture
    dr::Texture*     m_volumeTexture = nullptr;
    dr::TextureView* m_volumeView    = nullptr;

    // 1D LUT texture
    dr::Texture*     m_lutTexture = nullptr;
    dr::TextureView* m_lutView    = nullptr;

    dr::Sampler*         m_sampler  = nullptr;
    dr::BindGroupLayout* m_bgl     = nullptr;
    dr::BindGroup*       m_bg      = nullptr;
    dr::PipelineLayout*  m_pl      = nullptr;
    dr::RenderPipeline*  m_pipeline = nullptr;

    dr::CommandPool* m_pool     = nullptr;
    dr::Fence*       m_fence    = nullptr;
    raptor::core::u64       m_fenceVal = 0;
};

raptor::core::Status Texture3DSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"Vol3DVS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u8"PSMain", u8"Vol3DPS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (createVolumeTexture() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (createLUTTexture() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Sampler
    {
        dr::SamplerDesc sd{};
        sd.minFilter = dr::FilterMode::Linear;
        sd.magFilter = dr::FilterMode::Linear;
        sd.addressU = dr::AddressMode::Repeat;
        sd.addressV = dr::AddressMode::Repeat;
        sd.addressW = dr::AddressMode::Repeat;
        sd.label = u8"VolSampler";
        if (m_device->CreateSampler(sd, m_sampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Bind group layout: 3D tex, 1D tex, sampler
    {
        dr::BindGroupLayoutEntry entries[3] = {
            dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment, dr::TextureViewDimension::Texture3D),
            dr::BindGroupLayoutEntry::SampledTexture(1, dr::ShaderStage::Fragment, dr::TextureViewDimension::Texture1D),
            dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment)
        };
        dr::BindGroupLayoutDesc bgld{};
        bgld.entries = Span<const dr::BindGroupLayoutEntry>(entries, 3);
        bgld.label = u8"VolBGL";
        if (m_device->CreateBindGroupLayout(bgld, m_bgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Bind group
    {
        dr::BindGroupEntry entries[3] = {
            dr::BindGroupEntry::TextureEntry(m_volumeView),
            dr::BindGroupEntry::TextureEntry(m_lutView),
            dr::BindGroupEntry::SamplerEntry(m_sampler)
        };
        dr::BindGroupDesc bgd{};
        bgd.layout = m_bgl;
        bgd.entries = Span<const dr::BindGroupEntry>(entries, 3);
        bgd.label = u8"VolBG";
        if (m_device->CreateBindGroup(bgd, m_bg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Pipeline layout with push constants
    {
        dr::BindGroupLayout* sets[1] = { m_bgl };
        dr::PushConstantRange pcr{};
        pcr.stages = dr::ShaderStage::Fragment;
        pcr.offset = 0;
        pcr.size = sizeof(PushData);
        dr::PushConstantRange pushRanges[1] = { pcr };
        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
        pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
        pld.label = u8"VolPL";
        if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Render pipeline (fullscreen triangle, no vertex input)
    {
        dr::ColorTargetState ct{};
        ct.format = m_swapChain->Format();
        dr::RenderPipelineDesc rpd{};
        rpd.layout = m_pl;
        rpd.vertex.shader = { m_vs, u8"VSMain", dr::ShaderStage::Vertex };
        rpd.fragment = dr::FragmentState{};
        rpd.fragment->shader = { m_ps, u8"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
        rpd.label = u8"VolPipeline";
        if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status Texture3DSample::createVolumeTexture() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    dr::TextureDesc td{};
    td.dimension = dr::TextureDimension::Texture3D;
    td.format = dr::TextureFormat::R8Unorm;
    td.width = kVolumeSize;
    td.height = kVolumeSize;
    td.depth = kVolumeSize;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst;
    td.label = u8"VolumeTex3D";
    if (m_device->CreateTexture(td, m_volumeTexture) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TextureViewDesc tvd{};
    tvd.format = dr::TextureFormat::R8Unorm;
    tvd.dimension = dr::TextureViewDimension::Texture3D;
    if (m_device->CreateTextureView(m_volumeTexture, tvd, m_volumeView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Generate procedural 3D noise data
    constexpr u32 dataSize = kVolumeSize * kVolumeSize * kVolumeSize;
    u8 data[dataSize];

    for (u32 z = 0; z < kVolumeSize; z++) {
        for (u32 y = 0; y < kVolumeSize; y++) {
            for (u32 x = 0; x < kVolumeSize; x++) {
                float fx = static_cast<float>(x) / static_cast<float>(kVolumeSize);
                float fy = static_cast<float>(y) / static_cast<float>(kVolumeSize);
                float fz = static_cast<float>(z) / static_cast<float>(kVolumeSize);

                // Simple 3D pattern: spherical blobs + frequency pattern
                float cx = fx - 0.5f, cy = fy - 0.5f, cz = fz - 0.5f;
                float dist = std::sqrt(cx * cx + cy * cy + cz * cz);
                float sphere = std::max(0.0f, 1.0f - dist * 3.0f);
                float pattern = std::sin(fx * 12.0f) * std::sin(fy * 12.0f) * std::sin(fz * 12.0f);
                float v = std::clamp(sphere + pattern * 0.3f, 0.0f, 1.0f);

                u32 idx = z * kVolumeSize * kVolumeSize + y * kVolumeSize + x;
                data[idx] = static_cast<u8>(v * 255.0f);
            }
        }
    }

    dr::TransferBatch* batch = nullptr;
    m_graphicsQueue->CreateTransferBatch(batch);
    dr::TextureDataLayout layout{};
    layout.bytesPerRow = kVolumeSize;
    layout.rowsPerImage = kVolumeSize;
    batch->WriteTexture(m_volumeTexture,
        Span<const u8>(data, dataSize),
        layout, dr::Extent3D{kVolumeSize, kVolumeSize, kVolumeSize});
    batch->Submit();
    m_graphicsQueue->DestroyTransferBatch(batch);

    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status Texture3DSample::createLUTTexture() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    dr::TextureDesc td{};
    td.dimension = dr::TextureDimension::Texture1D;
    td.format = dr::TextureFormat::RGBA8UnormSrgb;
    td.width = kLUTSize;
    td.height = 1;
    td.arrayLayerCount = 1;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst;
    td.label = u8"LUTTex1D";
    if (m_device->CreateTexture(td, m_lutTexture) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TextureViewDesc tvd{};
    tvd.format = dr::TextureFormat::RGBA8UnormSrgb;
    tvd.dimension = dr::TextureViewDimension::Texture1D;
    if (m_device->CreateTextureView(m_lutTexture, tvd, m_lutView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Generate gradient LUT: dark blue -> cyan -> green -> yellow -> red -> white
    u8 data[kLUTSize * 4];
    for (u32 i = 0; i < kLUTSize; i++) {
        float t = static_cast<float>(i) / static_cast<float>(kLUTSize - 1);
        float r, g, b;
        if (t < 0.2f) {
            float s = t / 0.2f;
            r = 0.05f; g = 0.05f + s * 0.4f; b = 0.3f + s * 0.5f;
        } else if (t < 0.4f) {
            float s = (t - 0.2f) / 0.2f;
            r = 0.05f; g = 0.45f + s * 0.5f; b = 0.8f - s * 0.5f;
        } else if (t < 0.6f) {
            float s = (t - 0.4f) / 0.2f;
            r = s * 0.8f; g = 0.95f; b = 0.3f - s * 0.3f;
        } else if (t < 0.8f) {
            float s = (t - 0.6f) / 0.2f;
            r = 0.8f + s * 0.2f; g = 0.95f - s * 0.6f; b = 0.0f;
        } else {
            float s = (t - 0.8f) / 0.2f;
            r = 1.0f; g = 0.35f + s * 0.65f; b = s * 0.8f;
        }

        u32 idx = i * 4;
        data[idx + 0] = static_cast<u8>(r * 255.0f);
        data[idx + 1] = static_cast<u8>(g * 255.0f);
        data[idx + 2] = static_cast<u8>(b * 255.0f);
        data[idx + 3] = 255;
    }

    dr::TransferBatch* batch = nullptr;
    m_graphicsQueue->CreateTransferBatch(batch);
    dr::TextureDataLayout layout{};
    layout.bytesPerRow = kLUTSize * 4;
    layout.rowsPerImage = 1;
    batch->WriteTexture(m_lutTexture,
        Span<const u8>(data, kLUTSize * 4),
        layout, dr::Extent3D{kLUTSize, 1, 1});
    batch->Submit();
    m_graphicsQueue->DestroyTransferBatch(batch);

    return raptor::core::ErrorCode::Ok;
}

void Texture3DSample::OnRender() {
    using raptor::core::f32, raptor::core::Span;

    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{};
    ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear;
    ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    dr::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);

    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetPipeline(m_pipeline);
    rp->SetBindGroup(0, m_bg);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f);
    rp->SetScissor(0, 0, m_width, m_height);

    // Animate slice through 3D volume
    float sliceZ = 0.5f + 0.5f * std::sin(m_totalTime * 0.5f);
    PushData pc{};
    pc.sliceZ = sliceZ;
    pc.time = m_totalTime;
    rp->SetPushConstants(dr::ShaderStage::Fragment, 0, sizeof(PushData), &pc);

    rp->Draw(3); // Fullscreen triangle

    rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    dr::CommandBuffer* cb = enc->Finish();
    m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void Texture3DSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline);
    if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_bg) m_device->DestroyBindGroup(m_bg);
    if (m_bgl) m_device->DestroyBindGroupLayout(m_bgl);
    if (m_sampler) m_device->DestroySampler(m_sampler);
    if (m_lutView) m_device->DestroyTextureView(m_lutView);
    if (m_lutTexture) m_device->DestroyTexture(m_lutTexture);
    if (m_volumeView) m_device->DestroyTextureView(m_volumeView);
    if (m_volumeTexture) m_device->DestroyTexture(m_volumeTexture);
    if (m_ps) m_device->DestroyShaderModule(m_ps);
    if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { Texture3DSample app; return app.Run(argc, argv); }
