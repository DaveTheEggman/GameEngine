#include <new>
/// Sample023 -- Cube Map & Comparison Sampler. Ported from Sedulous Sample023_CubeMap.
/// Demonstrates cube map textures and comparison samplers.
/// Renders a fullscreen quad that samples a procedural cube map (skybox),
/// plus a second pass with a depth texture sampled via comparison sampler
/// to demonstrate shadow-map-style sampling.

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

class CubeMapSample : public sf::SampleApp {
public:
    using sf::SampleApp::SampleApp;
    raptor::core::StringView title() const override { return u"Sample023 - Cube Map & Comparison Sampler"; }
protected:
    raptor::core::Status onInit() override;
    void onRender() override;
    void onShutdown() override;
private:
    raptor::core::Status createCubeMap();
    raptor::core::Status createDepthTexture();

    // Skybox shader: fullscreen quad -> ray direction -> cube map lookup
    static constexpr const char8_t kSkyboxShader[] = u8R"(
        TextureCube<float4> gCubeMap : register(t0, space0);
        SamplerState gSampler : register(s0, space0);

        struct PushConstants
        {
            float Time;
            float AspectRatio;
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
            // Fullscreen triangle
            float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
            output.Position = float4(uv * 2.0 - 1.0, 0.5, 1.0);
            output.UV = uv;
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            // Convert UV to ray direction
            float2 ndc = input.UV * 2.0 - 1.0;
            ndc.x *= pc.AspectRatio;
            ndc.y = -ndc.y;

            // Simple rotation around Y
            float c = cos(pc.Time * 0.3);
            float s = sin(pc.Time * 0.3);

            float3 dir = normalize(float3(ndc.x, ndc.y, 1.0));
            float3 rotDir = float3(dir.x * c + dir.z * s, dir.y, -dir.x * s + dir.z * c);

            return gCubeMap.Sample(gSampler, rotDir);
        }
    )";

    // Shadow test shader: renders a quad, samples a depth texture with comparison sampler
    static constexpr const char8_t kShadowShader[] = u8R"(
        Texture2D<float> gShadowMap : register(t0, space0);
        SamplerComparisonState gShadowSampler : register(s0, space0);

        struct PushConstants
        {
            float Time;
            float AspectRatio;
            float2 _pad;
        };

        [[vk::push_constant]] ConstantBuffer<PushConstants> pc : register(b0, space1);

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

        PSInput VSMain(VSInput input)
        {
            PSInput output;
            output.Position = float4(input.Position, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }

        float4 PSMain(PSInput input) : SV_TARGET
        {
            // Compare at varying depth based on time for animated shadow boundary
            float compareValue = 0.5 + 0.4 * sin(pc.Time);
            float shadow = gShadowMap.SampleCmpLevelZero(gShadowSampler, input.TexCoord, compareValue);
            float3 litColor = float3(0.9, 0.85, 0.7);
            float3 shadowColor = float3(0.1, 0.1, 0.2);
            float3 color = lerp(shadowColor, litColor, shadow);
            return float4(color, 1.0);
        }
    )";

    struct PushData {
        float time;
        float aspectRatio;
        float _pad0;
        float _pad1;
    };

    ds::Compiler* compiler_ = nullptr;

    // Skybox resources
    dr::ShaderModule* skyboxVs_ = nullptr;
    dr::ShaderModule* skyboxPs_ = nullptr;
    dr::Texture*      cubeTexture_ = nullptr;
    dr::TextureView*  cubeView_ = nullptr;
    dr::Sampler*      linearSampler_ = nullptr;
    dr::BindGroupLayout* skyboxBgl_ = nullptr;
    dr::BindGroup*       skyboxBg_ = nullptr;
    dr::PipelineLayout*  skyboxPl_ = nullptr;
    dr::RenderPipeline*  skyboxPipeline_ = nullptr;

    // Shadow comparison resources
    dr::ShaderModule* shadowVs_ = nullptr;
    dr::ShaderModule* shadowPs_ = nullptr;
    dr::Texture*      depthTexture_ = nullptr;
    dr::TextureView*  depthView_ = nullptr;
    dr::Sampler*      comparisonSampler_ = nullptr;
    dr::Buffer*       quadVb_ = nullptr;
    dr::BindGroupLayout* shadowBgl_ = nullptr;
    dr::BindGroup*       shadowBg_ = nullptr;
    dr::PipelineLayout*  shadowPl_ = nullptr;
    dr::RenderPipeline*  shadowPipeline_ = nullptr;

    dr::CommandPool* pool_ = nullptr;
    dr::Fence*       fence_ = nullptr;
    raptor::core::u64       fenceVal_ = 0;
};

raptor::core::Status CubeMapSample::onInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, compiler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Compile skybox shaders
    if (sf::compileToModule(compiler_, device_, kSkyboxShader, ds::ShaderStage::Vertex,   u"VSMain", u"SkyboxVS", skyboxVs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kSkyboxShader, ds::ShaderStage::Fragment, u"PSMain", u"SkyboxPS", skyboxPs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Compile shadow shaders
    if (sf::compileToModule(compiler_, device_, kShadowShader, ds::ShaderStage::Vertex,   u"VSMain", u"ShadowVS", shadowVs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::compileToModule(compiler_, device_, kShadowShader, ds::ShaderStage::Fragment, u"PSMain", u"ShadowPS", shadowPs_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create procedural cube map (6 faces, 64x64, each a solid color)
    if (createCubeMap() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create depth texture for comparison sampler (gradient)
    if (createDepthTexture() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create samplers
    {
        dr::SamplerDesc sd{};
        sd.minFilter = dr::FilterMode::Linear;
        sd.magFilter = dr::FilterMode::Linear;
        sd.label = u"LinearSampler";
        if (device_->CreateSampler(sd, linearSampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }
    {
        dr::SamplerDesc sd{};
        sd.minFilter = dr::FilterMode::Linear;
        sd.magFilter = dr::FilterMode::Linear;
        sd.compare = dr::CompareFunction::LessEqual;
        sd.label = u"ComparisonSampler";
        if (device_->CreateSampler(sd, comparisonSampler_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox bind group layout: cube texture + sampler
    {
        dr::BindGroupLayoutEntry entries[2] = {
            dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, dr::TextureViewDimension::TextureCube),
            dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment)
        };
        dr::BindGroupLayoutDesc bgld{};
        bgld.entries = Span<const dr::BindGroupLayoutEntry>(entries, 2);
        bgld.label = u"SkyboxBGL";
        if (device_->CreateBindGroupLayout(bgld, skyboxBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox bind group
    {
        dr::BindGroupEntry entries[2] = {
            dr::BindGroupEntry::TextureEntry(cubeView_),
            dr::BindGroupEntry::SamplerEntry(linearSampler_)
        };
        dr::BindGroupDesc bgd{};
        bgd.layout = skyboxBgl_;
        bgd.entries = Span<const dr::BindGroupEntry>(entries, 2);
        bgd.label = u"SkyboxBG";
        if (device_->CreateBindGroup(bgd, skyboxBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox pipeline layout
    {
        dr::BindGroupLayout* sets[1] = { skyboxBgl_ };
        dr::PushConstantRange pcr{};
        pcr.stages = dr::ShaderStage::Vertex | dr::ShaderStage::Fragment;
        pcr.offset = 0;
        pcr.size = sizeof(PushData);
        dr::PushConstantRange pushRanges[1] = { pcr };
        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
        pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
        pld.label = u"SkyboxPL";
        if (device_->CreatePipelineLayout(pld, skyboxPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox pipeline (fullscreen triangle, no vertex input)
    {
        dr::ColorTargetState ct{};
        ct.format = swapChain_->Format();
        dr::RenderPipelineDesc rpd{};
        rpd.layout = skyboxPl_;
        rpd.vertex.shader = { skyboxVs_, u"VSMain", dr::ShaderStage::Vertex };
        rpd.fragment = dr::FragmentState{};
        rpd.fragment->shader = { skyboxPs_, u"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
        rpd.label = u"SkyboxPipeline";
        if (device_->CreateRenderPipeline(rpd, skyboxPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Shadow test quad vertices (bottom-right corner overlay)
    {
        float quadVerts[] = {
            // pos xyz, uv
             0.3f, -0.9f, 0.0f,   0.0f, 1.0f,
             0.9f, -0.9f, 0.0f,   1.0f, 1.0f,
             0.9f, -0.3f, 0.0f,   1.0f, 0.0f,
             0.3f, -0.9f, 0.0f,   0.0f, 1.0f,
             0.9f, -0.3f, 0.0f,   1.0f, 0.0f,
             0.3f, -0.3f, 0.0f,   0.0f, 0.0f
        };

        u32 vbSize = sizeof(quadVerts);
        dr::BufferDesc bd{};
        bd.size = vbSize;
        bd.usage = dr::BufferUsage::Vertex | dr::BufferUsage::CopyDst;
        bd.memory = dr::MemoryLocation::GpuOnly;
        bd.label = u"ShadowQuadVB";
        if (device_->CreateBuffer(bd, quadVb_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        dr::TransferBatch* batch = nullptr;
        graphicsQueue_->CreateTransferBatch(batch);
        batch->WriteBuffer(quadVb_, 0, Span<const u8>(reinterpret_cast<const u8*>(quadVerts), vbSize));
        batch->Submit();
        graphicsQueue_->DestroyTransferBatch(batch);
    }

    // Shadow bind group layout: depth texture + comparison sampler
    {
        dr::BindGroupLayoutEntry entries[2];
        entries[0] = dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Fragment, dr::TextureViewDimension::Texture2D);
        entries[1] = {};
        entries[1].binding = 0;
        entries[1].visibility = dr::ShaderStage::Fragment;
        entries[1].type = dr::BindingType::ComparisonSampler;
        dr::BindGroupLayoutDesc bgld{};
        bgld.entries = Span<const dr::BindGroupLayoutEntry>(entries, 2);
        bgld.label = u"ShadowBGL";
        if (device_->CreateBindGroupLayout(bgld, shadowBgl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Shadow bind group
    {
        dr::BindGroupEntry entries[2] = {
            dr::BindGroupEntry::TextureEntry(depthView_),
            dr::BindGroupEntry::SamplerEntry(comparisonSampler_)
        };
        dr::BindGroupDesc bgd{};
        bgd.layout = shadowBgl_;
        bgd.entries = Span<const dr::BindGroupEntry>(entries, 2);
        bgd.label = u"ShadowBG";
        if (device_->CreateBindGroup(bgd, shadowBg_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Shadow pipeline layout
    {
        dr::BindGroupLayout* sets[1] = { shadowBgl_ };
        dr::PushConstantRange pcr{};
        pcr.stages = dr::ShaderStage::Vertex | dr::ShaderStage::Fragment;
        pcr.offset = 0;
        pcr.size = sizeof(PushData);
        dr::PushConstantRange pushRanges[1] = { pcr };
        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
        pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
        pld.label = u"ShadowPL";
        if (device_->CreatePipelineLayout(pld, shadowPl_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Shadow pipeline
    {
        dr::VertexAttribute attrs[2] = {
            { dr::VertexFormat::Float32x3, 0, 0 },
            { dr::VertexFormat::Float32x2, 12, 1 }
        };
        dr::VertexBufferLayout vbl{};
        vbl.stride = 20;
        vbl.attributes = Span<const dr::VertexAttribute>(attrs, 2);

        dr::ColorTargetState ct{};
        ct.format = swapChain_->Format();

        dr::RenderPipelineDesc rpd{};
        rpd.layout = shadowPl_;
        rpd.vertex.shader = { shadowVs_, u"VSMain", dr::ShaderStage::Vertex };
        rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
        rpd.fragment = dr::FragmentState{};
        rpd.fragment->shader = { shadowPs_, u"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
        rpd.label = u"ShadowPipeline";
        if (device_->CreateRenderPipeline(rpd, shadowPipeline_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    if (device_->CreateCommandPool(dr::QueueType::Graphics, pool_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (device_->CreateFence(0, fence_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status CubeMapSample::createCubeMap() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    constexpr u32 faceSize = 64;
    constexpr u32 BytesPerPixel = 4;
    constexpr u32 faceBytes = faceSize * faceSize * BytesPerPixel;

    // Create cube map texture: 2D with 6 array layers
    dr::TextureDesc td{};
    td.dimension = dr::TextureDimension::Texture2D;
    td.format = dr::TextureFormat::RGBA8UnormSrgb;
    td.width = faceSize;
    td.height = faceSize;
    td.arrayLayerCount = 6;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.usage = dr::TextureUsage::Sampled | dr::TextureUsage::CopyDst;
    td.label = u"CubeMapTex";
    if (device_->CreateTexture(td, cubeTexture_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create cube view
    dr::TextureViewDesc tvd{};
    tvd.format = dr::TextureFormat::RGBA8UnormSrgb;
    tvd.dimension = dr::TextureViewDimension::TextureCube;
    tvd.baseMipLevel = 0;
    tvd.mipLevelCount = 1;
    tvd.baseArrayLayer = 0;
    tvd.arrayLayerCount = 6;
    if (device_->CreateTextureView(cubeTexture_, tvd, cubeView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Generate 6 face colors: +X red, -X cyan, +Y green, -Y magenta, +Z blue, -Z yellow
    u8 faceColors[6][4] = {
        {200, 60, 60, 255},    // +X: red
        {60, 200, 200, 255},   // -X: cyan
        {60, 200, 60, 255},    // +Y: green
        {200, 60, 200, 255},   // -Y: magenta
        {60, 60, 200, 255},    // +Z: blue
        {200, 200, 60, 255}    // -Z: yellow
    };

    u8 stagingBuf[faceBytes];
    dr::TransferBatch* batch = nullptr;
    graphicsQueue_->CreateTransferBatch(batch);

    for (int face = 0; face < 6; face++) {
        // Fill face with gradient from face color to white at center
        for (u32 y = 0; y < faceSize; y++) {
            for (u32 x = 0; x < faceSize; x++) {
                float fx = (static_cast<float>(x) / static_cast<float>(faceSize)) * 2.0f - 1.0f;
                float fy = (static_cast<float>(y) / static_cast<float>(faceSize)) * 2.0f - 1.0f;
                float dist = std::min(1.0f, std::sqrt(fx * fx + fy * fy));
                float t = 1.0f - dist * 0.5f;

                u32 idx = (y * faceSize + x) * BytesPerPixel;
                stagingBuf[idx + 0] = static_cast<u8>(faceColors[face][0] * t + 40 * (1.0f - t));
                stagingBuf[idx + 1] = static_cast<u8>(faceColors[face][1] * t + 40 * (1.0f - t));
                stagingBuf[idx + 2] = static_cast<u8>(faceColors[face][2] * t + 40 * (1.0f - t));
                stagingBuf[idx + 3] = 255;
            }
        }

        dr::TextureDataLayout layout{};
        layout.bytesPerRow = faceSize * BytesPerPixel;
        layout.rowsPerImage = faceSize;
        batch->WriteTexture(cubeTexture_,
            Span<const u8>(stagingBuf, faceBytes),
            layout, dr::Extent3D{faceSize, faceSize, 1},
            0, static_cast<u32>(face));
    }

    batch->Submit();
    graphicsQueue_->DestroyTransferBatch(batch);
    return raptor::core::ErrorCode::Ok;
}

raptor::core::Status CubeMapSample::createDepthTexture() {
    using raptor::core::Status;

    constexpr raptor::core::u32 texSize = 64;

    dr::TextureDesc td{};
    td.dimension = dr::TextureDimension::Texture2D;
    td.format = dr::TextureFormat::Depth32Float;
    td.width = texSize;
    td.height = texSize;
    td.arrayLayerCount = 1;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    td.usage = dr::TextureUsage::DepthStencil | dr::TextureUsage::Sampled;
    td.label = u"ShadowDepthTex";
    if (device_->CreateTexture(td, depthTexture_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TextureViewDesc tvd{};
    tvd.format = dr::TextureFormat::Depth32Float;
    tvd.dimension = dr::TextureViewDimension::Texture2D;
    if (device_->CreateTextureView(depthTexture_, tvd, depthView_) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // We'll render a gradient depth in a render pass
    // For simplicity, just clear to 0.5 so the comparison sampler has something to compare against
    // (A real sample would render shadow casters here)

    return raptor::core::ErrorCode::Ok;
}

void CubeMapSample::onRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;

    if (fenceVal_ > 0) fence_->Wait(fenceVal_, ~0ull);
    if (swapChain_->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    pool_->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (pool_->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    f32 aspect = static_cast<f32>(width_) / static_cast<f32>(height_);

    // Render a depth value into the shadow depth texture
    enc->TransitionTexture(depthTexture_, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    {
        dr::DepthStencilAttachment dsa{};
        dsa.view = depthView_;
        dsa.depthLoadOp = dr::LoadOp::Clear;
        dsa.depthStoreOp = dr::StoreOp::Store;
        dsa.depthClearValue = 0.5f;
        dr::RenderPassDesc rpd{};
        rpd.depthStencilAttachment = dsa;
        auto* rp = enc->BeginRenderPass(rpd);
        rp->End();
    }

    // Transition depth texture from DepthStencilWrite -> ShaderRead for sampling
    enc->TransitionTexture(depthTexture_, dr::ResourceState::DepthStencilWrite, dr::ResourceState::ShaderRead);

    // Transition swapchain
    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{};
    ca.view = swapChain_->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear;
    ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    dr::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetViewport(0, 0, static_cast<f32>(width_), static_cast<f32>(height_), 0.0f, 1.0f);
    rp->SetScissor(0, 0, width_, height_);

    // Draw skybox (fullscreen triangle, no VB needed - SV_VertexID)
    rp->SetPipeline(skyboxPipeline_);
    rp->SetBindGroup(0, skyboxBg_);
    PushData pc{};
    pc.time = totalTime_;
    pc.aspectRatio = aspect;
    rp->SetPushConstants(dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, sizeof(PushData), &pc);
    rp->Draw(3);

    // Draw shadow comparison overlay quad
    rp->SetPipeline(shadowPipeline_);
    rp->SetBindGroup(0, shadowBg_);
    rp->SetPushConstants(dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, sizeof(PushData), &pc);
    rp->SetVertexBuffer(0, quadVb_, 0);
    rp->Draw(6);

    rp->End();

    enc->TransitionTexture(swapChain_->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    // Transition depth texture back to DepthStencilWrite for next frame
    enc->TransitionTexture(depthTexture_, dr::ResourceState::ShaderRead, dr::ResourceState::DepthStencilWrite);

    dr::CommandBuffer* cb = enc->Finish();
    fenceVal_++;
    dr::CommandBuffer* cbs[1] = { cb };
    graphicsQueue_->Submit(Span<dr::CommandBuffer* const>(cbs, 1), fence_, fenceVal_);
    swapChain_->Present(graphicsQueue_);
    pool_->DestroyEncoder(enc);
}

void CubeMapSample::onShutdown() {
    if (fence_) device_->DestroyFence(fence_);
    if (pool_) device_->DestroyCommandPool(pool_);
    if (shadowPipeline_) device_->DestroyRenderPipeline(shadowPipeline_);
    if (skyboxPipeline_) device_->DestroyRenderPipeline(skyboxPipeline_);
    if (shadowPl_) device_->DestroyPipelineLayout(shadowPl_);
    if (skyboxPl_) device_->DestroyPipelineLayout(skyboxPl_);
    if (shadowBg_) device_->DestroyBindGroup(shadowBg_);
    if (skyboxBg_) device_->DestroyBindGroup(skyboxBg_);
    if (shadowBgl_) device_->DestroyBindGroupLayout(shadowBgl_);
    if (skyboxBgl_) device_->DestroyBindGroupLayout(skyboxBgl_);
    if (comparisonSampler_) device_->DestroySampler(comparisonSampler_);
    if (linearSampler_) device_->DestroySampler(linearSampler_);
    if (quadVb_) device_->DestroyBuffer(quadVb_);
    if (depthView_) device_->DestroyTextureView(depthView_);
    if (depthTexture_) device_->DestroyTexture(depthTexture_);
    if (cubeView_) device_->DestroyTextureView(cubeView_);
    if (cubeTexture_) device_->DestroyTexture(cubeTexture_);
    if (shadowPs_) device_->DestroyShaderModule(shadowPs_);
    if (shadowVs_) device_->DestroyShaderModule(shadowVs_);
    if (skyboxPs_) device_->DestroyShaderModule(skyboxPs_);
    if (skyboxVs_) device_->DestroyShaderModule(skyboxVs_);
    if (compiler_) { compiler_->Destroy(); delete compiler_; }
}

int main(int argc, char** argv) { CubeMapSample app; return app.run(argc, argv); }
