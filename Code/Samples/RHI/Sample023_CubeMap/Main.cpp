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
    raptor::core::StringView Title() const override { return u8"Sample023 - Cube Map & Comparison Sampler"; }
protected:
    raptor::core::Status OnInit() override;
    void OnRender() override;
    void OnShutdown() override;
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

    ds::Compiler* m_compiler = nullptr;

    // Skybox resources
    dr::ShaderModule* m_skyboxVs = nullptr;
    dr::ShaderModule* m_skyboxPs = nullptr;
    dr::Texture*      m_cubeTexture = nullptr;
    dr::TextureView*  m_cubeView = nullptr;
    dr::Sampler*      m_linearSampler = nullptr;
    dr::BindGroupLayout* m_skyboxBgl = nullptr;
    dr::BindGroup*       m_skyboxBg = nullptr;
    dr::PipelineLayout*  m_skyboxPl = nullptr;
    dr::RenderPipeline*  m_skyboxPipeline = nullptr;

    // Shadow comparison resources
    dr::ShaderModule* m_shadowVs = nullptr;
    dr::ShaderModule* m_shadowPs = nullptr;
    dr::Texture*      m_depthTexture = nullptr;
    dr::TextureView*  m_depthView = nullptr;
    dr::Sampler*      m_comparisonSampler = nullptr;
    dr::Buffer*       m_quadVb = nullptr;
    dr::BindGroupLayout* m_shadowBgl = nullptr;
    dr::BindGroup*       m_shadowBg = nullptr;
    dr::PipelineLayout*  m_shadowPl = nullptr;
    dr::RenderPipeline*  m_shadowPipeline = nullptr;

    dr::CommandPool* m_pool = nullptr;
    dr::Fence*       m_fence = nullptr;
    raptor::core::u64       m_fenceVal = 0;
};

raptor::core::Status CubeMapSample::OnInit() {
    using raptor::core::Status, raptor::core::Span, raptor::core::u8, raptor::core::u32;

    if (ds::createCompiler(ds::CompilerDesc{}, m_compiler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Compile skybox shaders
    if (sf::CompileToModule(m_compiler, m_device, kSkyboxShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"SkyboxVS", m_skyboxVs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kSkyboxShader, ds::ShaderStage::Fragment, u8"PSMain", u8"SkyboxPS", m_skyboxPs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Compile shadow shaders
    if (sf::CompileToModule(m_compiler, m_device, kShadowShader, ds::ShaderStage::Vertex,   u8"VSMain", u8"ShadowVS", m_shadowVs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (sf::CompileToModule(m_compiler, m_device, kShadowShader, ds::ShaderStage::Fragment, u8"PSMain", u8"ShadowPS", m_shadowPs) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create procedural cube map (6 faces, 64x64, each a solid color)
    if (createCubeMap() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create depth texture for comparison sampler (gradient)
    if (createDepthTexture() != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create samplers
    {
        dr::SamplerDesc sd{};
        sd.minFilter = dr::FilterMode::Linear;
        sd.magFilter = dr::FilterMode::Linear;
        sd.label = u8"LinearSampler";
        if (m_device->CreateSampler(sd, m_linearSampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }
    {
        dr::SamplerDesc sd{};
        sd.minFilter = dr::FilterMode::Linear;
        sd.magFilter = dr::FilterMode::Linear;
        sd.compare = dr::CompareFunction::LessEqual;
        sd.label = u8"ComparisonSampler";
        if (m_device->CreateSampler(sd, m_comparisonSampler) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox bind group layout: cube texture + sampler
    {
        dr::BindGroupLayoutEntry entries[2] = {
            dr::BindGroupLayoutEntry::SampledTexture(0, dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, dr::TextureViewDimension::TextureCube),
            dr::BindGroupLayoutEntry::Sampler(0, dr::ShaderStage::Fragment)
        };
        dr::BindGroupLayoutDesc bgld{};
        bgld.entries = Span<const dr::BindGroupLayoutEntry>(entries, 2);
        bgld.label = u8"SkyboxBGL";
        if (m_device->CreateBindGroupLayout(bgld, m_skyboxBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox bind group
    {
        dr::BindGroupEntry entries[2] = {
            dr::BindGroupEntry::TextureEntry(m_cubeView),
            dr::BindGroupEntry::SamplerEntry(m_linearSampler)
        };
        dr::BindGroupDesc bgd{};
        bgd.layout = m_skyboxBgl;
        bgd.entries = Span<const dr::BindGroupEntry>(entries, 2);
        bgd.label = u8"SkyboxBG";
        if (m_device->CreateBindGroup(bgd, m_skyboxBg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox pipeline layout
    {
        dr::BindGroupLayout* sets[1] = { m_skyboxBgl };
        dr::PushConstantRange pcr{};
        pcr.stages = dr::ShaderStage::Vertex | dr::ShaderStage::Fragment;
        pcr.offset = 0;
        pcr.size = sizeof(PushData);
        dr::PushConstantRange pushRanges[1] = { pcr };
        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
        pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
        pld.label = u8"SkyboxPL";
        if (m_device->CreatePipelineLayout(pld, m_skyboxPl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Skybox pipeline (fullscreen triangle, no vertex input)
    {
        dr::ColorTargetState ct{};
        ct.format = m_swapChain->Format();
        dr::RenderPipelineDesc rpd{};
        rpd.layout = m_skyboxPl;
        rpd.vertex.shader = { m_skyboxVs, u8"VSMain", dr::ShaderStage::Vertex };
        rpd.fragment = dr::FragmentState{};
        rpd.fragment->shader = { m_skyboxPs, u8"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
        rpd.label = u8"SkyboxPipeline";
        if (m_device->CreateRenderPipeline(rpd, m_skyboxPipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
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
        bd.label = u8"ShadowQuadVB";
        if (m_device->CreateBuffer(bd, m_quadVb) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

        dr::TransferBatch* batch = nullptr;
        m_graphicsQueue->CreateTransferBatch(batch);
        batch->WriteBuffer(m_quadVb, 0, Span<const u8>(reinterpret_cast<const u8*>(quadVerts), vbSize));
        batch->Submit();
        m_graphicsQueue->DestroyTransferBatch(batch);
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
        bgld.label = u8"ShadowBGL";
        if (m_device->CreateBindGroupLayout(bgld, m_shadowBgl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Shadow bind group
    {
        dr::BindGroupEntry entries[2] = {
            dr::BindGroupEntry::TextureEntry(m_depthView),
            dr::BindGroupEntry::SamplerEntry(m_comparisonSampler)
        };
        dr::BindGroupDesc bgd{};
        bgd.layout = m_shadowBgl;
        bgd.entries = Span<const dr::BindGroupEntry>(entries, 2);
        bgd.label = u8"ShadowBG";
        if (m_device->CreateBindGroup(bgd, m_shadowBg) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    // Shadow pipeline layout
    {
        dr::BindGroupLayout* sets[1] = { m_shadowBgl };
        dr::PushConstantRange pcr{};
        pcr.stages = dr::ShaderStage::Vertex | dr::ShaderStage::Fragment;
        pcr.offset = 0;
        pcr.size = sizeof(PushData);
        dr::PushConstantRange pushRanges[1] = { pcr };
        dr::PipelineLayoutDesc pld{};
        pld.bindGroupLayouts = Span<dr::BindGroupLayout* const>(sets, 1);
        pld.pushConstantRanges = Span<const dr::PushConstantRange>(pushRanges, 1);
        pld.label = u8"ShadowPL";
        if (m_device->CreatePipelineLayout(pld, m_shadowPl) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
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
        ct.format = m_swapChain->Format();

        dr::RenderPipelineDesc rpd{};
        rpd.layout = m_shadowPl;
        rpd.vertex.shader = { m_shadowVs, u8"VSMain", dr::ShaderStage::Vertex };
        rpd.vertex.buffers = Span<const dr::VertexBufferLayout>(&vbl, 1);
        rpd.fragment = dr::FragmentState{};
        rpd.fragment->shader = { m_shadowPs, u8"PSMain", dr::ShaderStage::Fragment };
        rpd.fragment->targets = Span<const dr::ColorTargetState>(&ct, 1);
        rpd.primitive.topology = dr::PrimitiveTopology::TriangleList;
        rpd.label = u8"ShadowPipeline";
        if (m_device->CreateRenderPipeline(rpd, m_shadowPipeline) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    }

    if (m_device->CreateCommandPool(dr::QueueType::Graphics, m_pool) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
    if (m_device->CreateFence(0, m_fence) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;
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
    td.label = u8"CubeMapTex";
    if (m_device->CreateTexture(td, m_cubeTexture) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // Create cube view
    dr::TextureViewDesc tvd{};
    tvd.format = dr::TextureFormat::RGBA8UnormSrgb;
    tvd.dimension = dr::TextureViewDimension::TextureCube;
    tvd.baseMipLevel = 0;
    tvd.mipLevelCount = 1;
    tvd.baseArrayLayer = 0;
    tvd.arrayLayerCount = 6;
    if (m_device->CreateTextureView(m_cubeTexture, tvd, m_cubeView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

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
    m_graphicsQueue->CreateTransferBatch(batch);

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
        batch->WriteTexture(m_cubeTexture,
            Span<const u8>(stagingBuf, faceBytes),
            layout, dr::Extent3D{faceSize, faceSize, 1},
            0, static_cast<u32>(face));
    }

    batch->Submit();
    m_graphicsQueue->DestroyTransferBatch(batch);
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
    td.label = u8"ShadowDepthTex";
    if (m_device->CreateTexture(td, m_depthTexture) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    dr::TextureViewDesc tvd{};
    tvd.format = dr::TextureFormat::Depth32Float;
    tvd.dimension = dr::TextureViewDimension::Texture2D;
    if (m_device->CreateTextureView(m_depthTexture, tvd, m_depthView) != raptor::core::ErrorCode::Ok) return raptor::core::ErrorCode::Unknown;

    // We'll render a gradient depth in a render pass
    // For simplicity, just clear to 0.5 so the comparison sampler has something to compare against
    // (A real sample would render shadow casters here)

    return raptor::core::ErrorCode::Ok;
}

void CubeMapSample::OnRender() {
    using raptor::core::f32, raptor::core::u32, raptor::core::Span;

    if (m_fenceVal > 0) m_fence->Wait(m_fenceVal, ~0ull);
    if (m_swapChain->AcquireNextImage() != raptor::core::ErrorCode::Ok) return;

    m_pool->Reset();
    dr::CommandEncoder* enc = nullptr;
    if (m_pool->CreateEncoder(enc) != raptor::core::ErrorCode::Ok || !enc) return;

    f32 aspect = static_cast<f32>(m_width) / static_cast<f32>(m_height);

    // Render a depth value into the shadow depth texture
    enc->TransitionTexture(m_depthTexture, dr::ResourceState::Undefined, dr::ResourceState::DepthStencilWrite);
    {
        dr::DepthStencilAttachment dsa{};
        dsa.view = m_depthView;
        dsa.depthLoadOp = dr::LoadOp::Clear;
        dsa.depthStoreOp = dr::StoreOp::Store;
        dsa.depthClearValue = 0.5f;
        dr::RenderPassDesc rpd{};
        rpd.depthStencilAttachment = dsa;
        auto* rp = enc->BeginRenderPass(rpd);
        rp->End();
    }

    // Transition depth texture from DepthStencilWrite -> ShaderRead for sampling
    enc->TransitionTexture(m_depthTexture, dr::ResourceState::DepthStencilWrite, dr::ResourceState::ShaderRead);

    // Transition swapchain
    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::Undefined, dr::ResourceState::RenderTarget);

    dr::ColorAttachment ca{};
    ca.view = m_swapChain->CurrentTextureView();
    ca.loadOp = dr::LoadOp::Clear;
    ca.storeOp = dr::StoreOp::Store;
    ca.clearValue = dr::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    dr::RenderPassDesc rpd{};
    rpd.colorAttachments.Add(ca);
    auto* rp = enc->BeginRenderPass(rpd);

    rp->SetViewport(0, 0, static_cast<f32>(m_width), static_cast<f32>(m_height), 0.0f, 1.0f);
    rp->SetScissor(0, 0, m_width, m_height);

    // Draw skybox (fullscreen triangle, no VB needed - SV_VertexID)
    rp->SetPipeline(m_skyboxPipeline);
    rp->SetBindGroup(0, m_skyboxBg);
    PushData pc{};
    pc.time = m_totalTime;
    pc.aspectRatio = aspect;
    rp->SetPushConstants(dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, sizeof(PushData), &pc);
    rp->Draw(3);

    // Draw shadow comparison overlay quad
    rp->SetPipeline(m_shadowPipeline);
    rp->SetBindGroup(0, m_shadowBg);
    rp->SetPushConstants(dr::ShaderStage::Vertex | dr::ShaderStage::Fragment, 0, sizeof(PushData), &pc);
    rp->SetVertexBuffer(0, m_quadVb, 0);
    rp->Draw(6);

    rp->End();

    enc->TransitionTexture(m_swapChain->CurrentTexture(), dr::ResourceState::RenderTarget, dr::ResourceState::Present);

    // Transition depth texture back to DepthStencilWrite for next frame
    enc->TransitionTexture(m_depthTexture, dr::ResourceState::ShaderRead, dr::ResourceState::DepthStencilWrite);

    dr::CommandBuffer* cb = enc->Finish();
    m_fenceVal++;
    dr::CommandBuffer* cbs[1] = { cb };
    m_graphicsQueue->Submit(Span<dr::CommandBuffer* const>(cbs, 1), m_fence, m_fenceVal);
    m_swapChain->Present(m_graphicsQueue);
    m_pool->DestroyEncoder(enc);
}

void CubeMapSample::OnShutdown() {
    if (m_fence) m_device->DestroyFence(m_fence);
    if (m_pool) m_device->DestroyCommandPool(m_pool);
    if (m_shadowPipeline) m_device->DestroyRenderPipeline(m_shadowPipeline);
    if (m_skyboxPipeline) m_device->DestroyRenderPipeline(m_skyboxPipeline);
    if (m_shadowPl) m_device->DestroyPipelineLayout(m_shadowPl);
    if (m_skyboxPl) m_device->DestroyPipelineLayout(m_skyboxPl);
    if (m_shadowBg) m_device->DestroyBindGroup(m_shadowBg);
    if (m_skyboxBg) m_device->DestroyBindGroup(m_skyboxBg);
    if (m_shadowBgl) m_device->DestroyBindGroupLayout(m_shadowBgl);
    if (m_skyboxBgl) m_device->DestroyBindGroupLayout(m_skyboxBgl);
    if (m_comparisonSampler) m_device->DestroySampler(m_comparisonSampler);
    if (m_linearSampler) m_device->DestroySampler(m_linearSampler);
    if (m_quadVb) m_device->DestroyBuffer(m_quadVb);
    if (m_depthView) m_device->DestroyTextureView(m_depthView);
    if (m_depthTexture) m_device->DestroyTexture(m_depthTexture);
    if (m_cubeView) m_device->DestroyTextureView(m_cubeView);
    if (m_cubeTexture) m_device->DestroyTexture(m_cubeTexture);
    if (m_shadowPs) m_device->DestroyShaderModule(m_shadowPs);
    if (m_shadowVs) m_device->DestroyShaderModule(m_shadowVs);
    if (m_skyboxPs) m_device->DestroyShaderModule(m_skyboxPs);
    if (m_skyboxVs) m_device->DestroyShaderModule(m_skyboxVs);
    if (m_compiler) { m_compiler->Destroy(); delete m_compiler; }
}

int main(int argc, char** argv) { CubeMapSample app; return app.Run(argc, argv); }
