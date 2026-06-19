#include <new>
/// Sample018 -- Bindless Textures. Ported from Sedulous Sample018_Bindless.
/// Demonstrates bindless texture arrays with material index via push constants.
/// Creates 4 procedural textures, binds them in a bindless array, and renders
/// 4 quads each selecting a different texture via push constant index.

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

class BindlessSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::WideStringView Title() const override { return u"Sample018 - Bindless Textures"; }
    dr::DeviceFeatures RequiredFeatures() const override {
        dr::DeviceFeatures f{}; f.bindlessDescriptors = true; return f;
    }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
private:
    raptor::core::Status createTextures();
    void generatePixel(raptor::core::u32 texIndex, raptor::core::u32 x, raptor::core::u32 y, raptor::core::u8* rgba);

    static constexpr const char8_t kShader[] = u8R"(
        Texture2D gTextures[] : register(t0, space0);
        SamplerState gSampler : register(s0, space1);

        struct PushData
        {
            uint TextureIndex;
            float OffsetX;
            float OffsetY;
            float Padding;
        };

        [[vk::push_constant]] ConstantBuffer<PushData> gPush : register(b0, space2);

        struct PSInput
        {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };

        PSInput VSMain(uint vertexID : SV_VertexID)
        {
            // Fullscreen-quad-style: 4 vertices for a unit quad
            float2 positions[4] = {
                float2(-0.4, 0.4),
                float2( 0.4, 0.4),
                float2(-0.4,-0.4),
                float2( 0.4,-0.4)
            };
            float2 uvs[4] = {
                float2(0, 0), float2(1, 0),
                float2(0, 1), float2(1, 1)
            };

            PSInput output;
            float2 pos = positions[vertexID];
            pos.x += gPush.OffsetX;
            pos.y += gPush.OffsetY;
            output.Position = float4(pos, 0.0, 1.0);
            output.TexCoord = uvs[vertexID];
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            return gTextures[gPush.TextureIndex].Sample(gSampler, input.TexCoord);
        }
    )";

    static constexpr raptor::core::u32 kTexSize = 64;
    static constexpr raptor::core::u32 kNumTextures = 4;

    ds::Compiler* m_compiler = nullptr;
    dr::ShaderModule* m_vs = nullptr;
    dr::ShaderModule* m_ps = nullptr;

    // Textures
    dr::Texture*     m_textures[kNumTextures]     = {};
    dr::TextureView* m_textureViews[kNumTextures] = {};
    dr::Sampler* m_sampler = nullptr;

    // Bindless bind group (space0: bindless textures)
    dr::BindGroupLayout* m_bindlessBgl = nullptr;
    dr::BindGroup*       m_bindlessBg  = nullptr;

    // Sampler bind group (space1: sampler)
    dr::BindGroupLayout* m_samplerBgl = nullptr;
    dr::BindGroup*       m_samplerBg  = nullptr;

    dr::PipelineLayout*  m_pl       = nullptr;
    dr::RenderPipeline*  m_pipeline = nullptr;
    dr::CommandPool*     m_pool     = nullptr;
    dr::Fence*           m_fence    = nullptr;
    raptor::core::u64           m_fenceVal = 0;
};

raptor::core::Status BindlessSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"BindlessVS", m_vs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShader, ds::ShaderStage::Fragment, u"PSMain", u"BindlessPS", m_ps) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create 4 procedural textures with different patterns
    if (createTextures() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Sampler
    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Linear; sd.magFilter = dr::FilterMode::Linear;
    sd.addressU = dr::AddressMode::Repeat; sd.addressV = dr::AddressMode::Repeat;
    sd.label = u"BindlessSampler";
    if (m_device->CreateSampler(sd, m_sampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Bindless BGL (space0): unbounded texture array
    dr::BindGroupLayoutEntry bindlessEntry{};
    bindlessEntry.binding    = 0;
    bindlessEntry.visibility = dr::ShaderStage::Fragment;
    bindlessEntry.type       = dr::BindingType::BindlessTextures;
    bindlessEntry.textureDimension = dr::TextureViewDimension::Texture2D;
    bindlessEntry.count      = 0xFFFFFFFF;
    dr::BindGroupLayoutEntry blEntries[1] = { bindlessEntry };
    dr::BindGroupLayoutDesc blBgld{}; blBgld.entries = Span<const dr::BindGroupLayoutEntry>(blEntries, 1);
    blBgld.label = u"BindlessBGL";
    if (m_device->CreateBindGroupLayout(blBgld, m_bindlessBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create bindless bind group (no entries at creation - populated via updateBindless)
    dr::BindGroupDesc blBgd{}; blBgd.layout = m_bindlessBgl; blBgd.label = u"BindlessBG";
    if (m_device->CreateBindGroup(blBgd, m_bindlessBg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Populate bindless slots
    dr::BindlessUpdateEntry bindlessUpdates[kNumTextures];
    for (u32 i = 0; i < kNumTextures; ++i) {
        bindlessUpdates[i] = {};
        bindlessUpdates[i].layoutIndex = 0;
        bindlessUpdates[i].arrayIndex  = i;
        bindlessUpdates[i].textureView = m_textureViews[i];
    }
    m_bindlessBg->UpdateBindless(Span<const dr::BindlessUpdateEntry>(bindlessUpdates, kNumTextures));

    // Sampler BGL (space1)
    dr::BindGroupLayoutEntry samplerEntry = dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment);
    dr::BindGroupLayoutEntry sEntries[1] = { samplerEntry };
    dr::BindGroupLayoutDesc sBgld{}; sBgld.entries = Span<const dr::BindGroupLayoutEntry>(sEntries, 1);
    sBgld.label = u"SamplerBGL";
    if (m_device->CreateBindGroupLayout(sBgld, m_samplerBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry sBgEntries[1] = { dr::BindGroupEntry::SamplerEntry(m_sampler) };
    dr::BindGroupDesc sBgd{}; sBgd.layout = m_samplerBgl;
    sBgd.entries = Span<const dr::BindGroupEntry>(sBgEntries, 1); sBgd.label = u"SamplerBG";
    if (m_device->CreateBindGroup(sBgd, m_samplerBg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout: group 0 = bindless textures, group 1 = sampler, push constants
    dr::BindGroupLayout* sets[2] = { m_bindlessBgl, m_samplerBgl };
    dr::PushConstantRange pcr{}; pcr.stages = dr::ShaderStage::Vertex | dr::ShaderStage::Fragment;
    pcr.offset = 0; pcr.size = 16;
    dr::PushConstantRange pushRanges[1] = { pcr };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
    pld.label = u"BindlessPL";
    if (m_device->CreatePipelineLayout(pld, m_pl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline (no vertex buffers - SV_VertexID driven)
    dr::ColorTargetState ct{}; ct.format = m_swapChain->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = m_pl;
    rpd.vertex.shader = { m_vs, u"VSMain", dr::ShaderStage::Vertex };
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { m_ps, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::TriangleStrip;
    rpd.label = u"BindlessPipeline";
    if (m_device->CreateRenderPipeline(rpd, m_pipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status BindlessSample::createTextures() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    constexpr u32 rowBytes = kTexSize * 4;
    constexpr u32 texBytes = rowBytes * kTexSize;
    u8 pixels[texBytes];

    dr::TransferBatch* batch = nullptr;
    m_graphicsQueue->CreateTransferBatch(batch);

    for (u32 t = 0; t < kNumTextures; ++t) {
        // Generate pattern
        for (u32 y = 0; y < kTexSize; ++y) {
            for (u32 x = 0; x < kTexSize; ++x) {
                u32 offset = (y * kTexSize + x) * 4;
                generatePixel(t, x, y, &pixels[offset]);
            }
        }

        dr::TextureDesc td{}; td.format = dr::TextureFormat::RGBA8Unorm;
        td.width = kTexSize; td.height = kTexSize;
        td.mipLevelCount = 1; td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst;
        td.label = u"BindlessTex";
        if (m_device->CreateTexture(td, m_textures[t]) != raptor::core::ErrorCode::Ok) {
            m_graphicsQueue->DestroyTransferBatch(batch); return raptor::core::ErrorCode::Unknown;
        }

        dr::TextureDataLayout layout{}; layout.bytesPerRow = rowBytes; layout.rowsPerImage = kTexSize;
        batch->WriteTexture(m_textures[t], Span<const u8>(pixels, texBytes),
            layout, dr::Extent3D{kTexSize, kTexSize, 1});

        dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm;
        tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
        if (m_device->CreateTextureView(m_textures[t], tvd, m_textureViews[t]) != raptor::core::ErrorCode::Ok) {
            m_graphicsQueue->DestroyTransferBatch(batch); return raptor::core::ErrorCode::Unknown;
        }
    }

    batch->Submit();
    m_graphicsQueue->DestroyTransferBatch(batch);
    return raptor::core::ErrorCode::Ok;
}

void BindlessSample::generatePixel(raptor::core::u32 texIndex, raptor::core::u32 x, raptor::core::u32 y, raptor::core::u8* rgba) {
    float fx = static_cast<float>(x) / static_cast<float>(kTexSize);
    float fy = static_cast<float>(y) / static_cast<float>(kTexSize);

    switch (texIndex) {
    case 0: { // Red/white checkerboard
        bool check = ((x / 8) + (y / 8)) % 2 == 0;
        rgba[0] = check ? 220 : 255;
        rgba[1] = check ? 30  : 255;
        rgba[2] = check ? 30  : 255;
        rgba[3] = 255;
        break;
    }
    case 1: { // Green gradient with stripes
        auto g = static_cast<raptor::core::u8>(fx * 255.0f);
        bool stripe = (y % 16) < 8;
        rgba[0] = stripe ? 30 : 10;
        rgba[1] = stripe ? g  : static_cast<raptor::core::u8>(g / 2);
        rgba[2] = stripe ? 50 : 30;
        rgba[3] = 255;
        break;
    }
    case 2: { // Blue circles
        float cx = fx - 0.5f, cy = fy - 0.5f;
        float dist = std::sqrt(cx * cx + cy * cy);
        float rings = std::sin(dist * 30.0f) * 0.5f + 0.5f;
        rgba[0] = static_cast<raptor::core::u8>(rings * 60);
        rgba[1] = static_cast<raptor::core::u8>(rings * 100);
        rgba[2] = static_cast<raptor::core::u8>(rings * 255);
        rgba[3] = 255;
        break;
    }
    default: { // Yellow/purple diagonal
        float diag = std::sin((fx + fy) * 10.0f) * 0.5f + 0.5f;
        rgba[0] = static_cast<raptor::core::u8>(diag * 255 + (1.0f - diag) * 120);
        rgba[1] = static_cast<raptor::core::u8>(diag * 220);
        rgba[2] = static_cast<raptor::core::u8>((1.0f - diag) * 200);
        rgba[3] = 255;
        break;
    }
    }
}

void BindlessSample::OnRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.06f, 0.12f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetPipeline(m_pipeline);
    rp->SetBindGroup(0, m_bindlessBg);
    rp->SetBindGroup(1, m_samplerBg);
    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f);
    rp->SetScissor(0, 0, m_width, m_height);

    // Draw 4 quads, each with a different texture index via push constants
    // Layout: 2x2 grid
    float offsets[8] = { -0.45f, 0.45f, 0.45f, 0.45f, -0.45f, -0.45f, 0.45f, -0.45f };

    for (u32 i = 0; i < kNumTextures; ++i) {
        u32 pushData[4] = { i, 0, 0, 0 };
        std::memcpy(&pushData[1], &offsets[i * 2],     4);
        std::memcpy(&pushData[2], &offsets[i * 2 + 1], 4);
        rp->SetPushConstants(dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, 16, pushData);
        rp->Draw(4);
    }

    rp->End();
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void BindlessSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_pipeline) m_device->DestroyRenderPipeline(m_pipeline);
    if (m_pl) m_device->DestroyPipelineLayout(m_pl);
    if (m_samplerBg) m_device->DestroyBindGroup(m_samplerBg);
    if (m_samplerBgl) m_device->DestroyBindGroupLayout(m_samplerBgl);
    if (m_bindlessBg) m_device->DestroyBindGroup(m_bindlessBg);
    if (m_bindlessBgl) m_device->DestroyBindGroupLayout(m_bindlessBgl);
    if (m_sampler) m_device->DestroySampler(m_sampler);
    for (int i = kNumTextures - 1; i >= 0; --i) {
        if (m_textureViews[i]) m_device->DestroyTextureView(m_textureViews[i]);
        if (m_textures[i]) m_device->DestroyTexture(m_textures[i]);
    }
    if (m_ps) m_device->DestroyShaderModule(m_ps);
    if (m_vs) m_device->DestroyShaderModule(m_vs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { BindlessSample app; return app.Run(argc, argv); }
