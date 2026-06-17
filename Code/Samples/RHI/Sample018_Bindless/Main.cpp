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
    raptor::core::StringView title() const override { return u"Sample018 - Bindless Textures"; }
    dr::DeviceFeatures requiredFeatures() const override {
        dr::DeviceFeatures f{}; f.bindlessDescriptors = true; return f;
    }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
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

    ds::Compiler* compiler_ = nullptr;
    dr::ShaderModule* vs_ = nullptr;
    dr::ShaderModule* ps_ = nullptr;

    // Textures
    dr::Texture*     textures_[kNumTextures]     = {};
    dr::TextureView* textureViews_[kNumTextures] = {};
    dr::Sampler* sampler_ = nullptr;

    // Bindless bind group (space0: bindless textures)
    dr::BindGroupLayout* bindlessBgl_ = nullptr;
    dr::BindGroup*       bindlessBg_  = nullptr;

    // Sampler bind group (space1: sampler)
    dr::BindGroupLayout* samplerBgl_ = nullptr;
    dr::BindGroup*       samplerBg_  = nullptr;

    dr::PipelineLayout*  pl_       = nullptr;
    dr::RenderPipeline*  pipeline_ = nullptr;
    dr::CommandPool*     pool_     = nullptr;
    dr::Fence*           fence_    = nullptr;
    raptor::core::u64           fenceVal_ = 0;
};

raptor::core::Status BindlessSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Vertex,   u"VSMain", u"BindlessVS", vs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShader, ds::ShaderStage::Fragment, u"PSMain", u"BindlessPS", ps_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create 4 procedural textures with different patterns
    if (createTextures() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Sampler
    dr::SamplerDesc sd{}; sd.minFilter = dr::FilterMode::Linear; sd.magFilter = dr::FilterMode::Linear;
    sd.addressU = dr::AddressMode::Repeat; sd.addressV = dr::AddressMode::Repeat;
    sd.label = u"BindlessSampler";
    if (device_->CreateSampler(sd, sampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

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
    if (device_->CreateBindGroupLayout(blBgld, bindlessBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create bindless bind group (no entries at creation - populated via updateBindless)
    dr::BindGroupDesc blBgd{}; blBgd.layout = bindlessBgl_; blBgd.label = u"BindlessBG";
    if (device_->CreateBindGroup(blBgd, bindlessBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Populate bindless slots
    dr::BindlessUpdateEntry bindlessUpdates[kNumTextures];
    for (u32 i = 0; i < kNumTextures; ++i) {
        bindlessUpdates[i] = {};
        bindlessUpdates[i].layoutIndex = 0;
        bindlessUpdates[i].arrayIndex  = i;
        bindlessUpdates[i].textureView = textureViews_[i];
    }
    bindlessBg_->UpdateBindless(Span<const dr::BindlessUpdateEntry>(bindlessUpdates, kNumTextures));

    // Sampler BGL (space1)
    dr::BindGroupLayoutEntry samplerEntry = dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment);
    dr::BindGroupLayoutEntry sEntries[1] = { samplerEntry };
    dr::BindGroupLayoutDesc sBgld{}; sBgld.entries = Span<const dr::BindGroupLayoutEntry>(sEntries, 1);
    sBgld.label = u"SamplerBGL";
    if (device_->CreateBindGroupLayout(sBgld, samplerBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::BindGroupEntry sBgEntries[1] = { dr::BindGroupEntry::SamplerEntry(sampler_) };
    dr::BindGroupDesc sBgd{}; sBgd.layout = samplerBgl_;
    sBgd.entries = Span<const dr::BindGroupEntry>(sBgEntries, 1); sBgd.label = u"SamplerBG";
    if (device_->CreateBindGroup(sBgd, samplerBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Pipeline layout: group 0 = bindless textures, group 1 = sampler, push constants
    dr::BindGroupLayout* sets[2] = { bindlessBgl_, samplerBgl_ };
    dr::PushConstantRange pcr{}; pcr.stages = dr::ShaderStage::Vertex | dr::ShaderStage::Fragment;
    pcr.offset = 0; pcr.size = 16;
    dr::PushConstantRange pushRanges[1] = { pcr };
    dr::PipelineLayoutDesc pld{}; pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 2);
    pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
    pld.label = u"BindlessPL";
    if (device_->CreatePipelineLayout(pld, pl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Render pipeline (no vertex buffers - SV_VertexID driven)
    dr::ColorTargetState ct{}; ct.format = swapChain_->Format();
    dr::RenderPipelineDesc rpd{}; rpd.layout = pl_;
    rpd.vertex.shader = { vs_, u"VSMain", dr::ShaderStage::Vertex };
    rpd.fragment = dr::FragmentState{}; rpd.fragment->shader = { ps_, u"PSMain", dr::ShaderStage::Fragment };
    rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
    rpd.primitive.topology = dr::PrimitiveTopology::TriangleStrip;
    rpd.label = u"BindlessPipeline";
    if (device_->CreateRenderPipeline(rpd, pipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status BindlessSample::createTextures() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    constexpr u32 rowBytes = kTexSize * 4;
    constexpr u32 texBytes = rowBytes * kTexSize;
    u8 pixels[texBytes];

    dr::TransferBatch* batch = nullptr;
    graphicsQueue_->CreateTransferBatch(batch);

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
        if (device_->CreateTexture(td, textures_[t]) != raptor::core::ErrorCode::Ok) {
            graphicsQueue_->DestroyTransferBatch(batch); return raptor::core::ErrorCode::Unknown;
        }

        dr::TextureDataLayout layout{}; layout.bytesPerRow = rowBytes; layout.rowsPerImage = kTexSize;
        batch->WriteTexture(textures_[t], Span<const u8>(pixels, texBytes),
            layout, dr::Extent3D{kTexSize, kTexSize, 1});

        dr::TextureViewDesc tvd{}; tvd.format = dr::TextureFormat::RGBA8Unorm;
        tvd.mipLevelCount = 1; tvd.arrayLayerCount = 1;
        if (device_->CreateTextureView(textures_[t], tvd, textureViews_[t]) != raptor::core::ErrorCode::Ok) {
            graphicsQueue_->DestroyTransferBatch(batch); return raptor::core::ErrorCode::Unknown;
        }
    }

    batch->Submit();
    graphicsQueue_->DestroyTransferBatch(batch);
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

void BindlessSample::onRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;
    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;
    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{}; ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear; ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.08f, 0.06f, 0.12f, 1.0f);
    dr::RenderPassDesc rpd{}; rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetPipeline(pipeline_);
    rp->SetBindGroup(0, bindlessBg_);
    rp->SetBindGroup(1, samplerBg_);
    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0.0f, 1.0f);
    rp->SetScissor(0, 0, width_, height_);

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
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);
    dr::CommandBuffer* cb = enc->Finish(); fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void BindlessSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (pipeline_) device_->DestroyRenderPipeline(pipeline_);
    if (pl_) device_->DestroyPipelineLayout(pl_);
    if (samplerBg_) device_->DestroyBindGroup(samplerBg_);
    if (samplerBgl_) device_->DestroyBindGroupLayout(samplerBgl_);
    if (bindlessBg_) device_->DestroyBindGroup(bindlessBg_);
    if (bindlessBgl_) device_->DestroyBindGroupLayout(bindlessBgl_);
    if (sampler_) device_->DestroySampler(sampler_);
    for (int i = kNumTextures - 1; i >= 0; --i) {
        if (textureViews_[i]) device_->DestroyTextureView(textureViews_[i]);
        if (textures_[i]) device_->DestroyTexture(textures_[i]);
    }
    if (ps_) device_->DestroyShaderModule(ps_);
    if (vs_) device_->DestroyShaderModule(vs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { BindlessSample app; return app.run(argc, argv); }
