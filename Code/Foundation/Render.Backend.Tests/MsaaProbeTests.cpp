// Scene-pass MSAA acceptance probe (msaa.md P1g). Render a high-contrast opaque silhouette (a
// bright, rotated cube on a black background) through the FULL RenderFrame chain - forward + the
// MsaaResolvePass + tonemap - on real Vulkan and WebGPU, once at 1x and once at 4x, read the final
// LDR pixels back, and assert the 4x image has a fringe of INTERMEDIATE-luma pixels along the
// silhouette (partial coverage from the resolve) that the hard-edged 1x image does not. Structural
// probe (no golden image); shares the readback substrate with the other backend tests.
//
// Cooked-pack WGSL path too (the browser's shaders, on wgpu-native):
//   OPTION_USE_SHADER_PACK=1 ENV_WEBGPU_WGSL=1 ./Render.Backend.Tests
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <cstdio>

import foundation.core;
import foundation.rhi;
import foundation.rhi.vulkan;
import foundation.rhi.webgpu;
import foundation.rhi.testsupport;
import foundation.geometry;
import foundation.materials;
import foundation.materials.pipelinecache;
import foundation.shaders;
import foundation.shaders.system;
import foundation.render;
import foundation.rendergraph;

using namespace foundation::core;
using namespace foundation::render;
namespace rhi = foundation::rhi;
namespace testsupport = foundation::rhi::testsupport;
namespace geometry = foundation::geometry;
namespace materials = foundation::materials;
namespace shaders = foundation::shaders;

namespace
{
    constexpr u32 kSize = 128;

    // Render the silhouette scene at `sampleCount` and read the final LDR image back.
    testsupport::CapturedImage RenderMsaa(rhi::Device& device, u32 sampleCount)
    {
        testsupport::CapturedImage out;
        shaders::ShaderSystemHost host;
        if (!host.Initialize(device, StringView(reinterpret_cast<const char8_t*>(
                                         BUILTIN_ENGINE_SHADER_DIR))))
        {
            return out;
        }
        {
            shaders::ShaderSystem& shaderSystem = *host.System();
            materials::PipelineStateCache psoCache(shaderSystem, device);
            materials::MaterialSystem materialSystem;
            REQUIRE(materialSystem.Initialize(device).IsOk());
            MeshRenderer meshRenderer(device, shaderSystem, psoCache, materialSystem, 2);
            REQUIRE(meshRenderer.Initialize().IsOk());
            RendererRegistry registry;
            registry.Register(&meshRenderer);

            TonemapPass tonemap(device, shaderSystem, 2);
            REQUIRE(tonemap.Initialize().IsOk());
            MsaaResolvePass msaaResolve(device, shaderSystem);
            REQUIRE(msaaResolve.Initialize().IsOk());

            RenderFrame frame(device, registry, 2, /*clusters*/ nullptr, &tonemap, /*shadows*/ nullptr,
                              /*ibl*/ nullptr, /*sky*/ nullptr, /*bloom*/ nullptr, /*taa*/ nullptr,
                              /*ao*/ nullptr, /*fxaa*/ nullptr);
            frame.SetMsaaResolve(&msaaResolve);

            // A bright white cube, ROTATED so its silhouette is diagonal (long edges = a clear
            // coverage signal), on a black clear. Flat bright ambient - no lights needed.
            RefPtr<geometry::StaticMesh> cubeMesh = geometry::Primitives::Cube(2.2f);
            RefPtr<materials::Material> cubeMat =
                materials::CreatePBR(u8"msaa.cube", Float4{1, 1, 1, 1}, 0.0f, 0.6f);
            ExtractedScene scene;
            scene.SetAmbient(Float3{1.0f, 1.0f, 1.0f});
            MeshRenderData* cube = scene.Add<MeshRenderData>();
            cube->world = Float4x4::RotationY(0.6f) * Float4x4::RotationX(0.5f);
            cube->worldCenter = Float3{0.0f, 0.0f, 0.0f};
            cube->mesh = cubeMesh.Get();
            cube->material = cubeMat.Get();
            cube->category = RenderCategories::Opaque;

            ViewCamera camera;
            camera.view = Float4x4::LookAtRH(Float3{0, 0, 6}, Float3{0, 0, 0}, Float3{0, 1, 0});
            camera.projection = Float4x4::PerspectiveFovRH(1.0472f, 1.0f, 0.1f, 100.0f);

            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA8Unorm;
            td.width = kSize;
            td.height = kSize;
            td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
            td.label = u8"msaa.probe.target";
            rhi::Texture* target = nullptr;
            REQUIRE(device.CreateTexture(td, target).IsOk());
            rhi::TextureViewDesc vd{};
            vd.format = rhi::TextureFormat::RGBA8Unorm;
            rhi::TextureView* targetView = nullptr;
            REQUIRE(device.CreateTextureView(target, vd, targetView).IsOk());

            rhi::CommandPool* pool = nullptr;
            REQUIRE(device.CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk());
            rhi::Fence* fence = nullptr;
            REQUIRE(device.CreateFence(0, fence).IsOk());
            rhi::Queue* queue = device.GetQueue(rhi::QueueType::Graphics);
            REQUIRE(queue != nullptr);

            ViewSettings settings;
            settings.clear = rhi::ClearColor::Black();
            settings.targetTexture = target;
            settings.targetFinalState = rhi::ResourceState::CopySrc;
            settings.post.taaEnabled = false;
            settings.post.bloomEnabled = false;
            settings.post.msaaSamples = static_cast<u8>(sampleCount);

            for (u32 i = 0; i < 2; ++i)
            {
                rhi::CommandEncoder* encoder = nullptr;
                REQUIRE(pool->CreateEncoder(encoder).IsOk());
                settings.targetCurrentState =
                    (i == 0) ? rhi::ResourceState::Undefined : rhi::ResourceState::CopySrc;
                frame.Begin(*encoder, i % 2);
                frame.AddView(scene, camera, settings, targetView, rhi::TextureFormat::RGBA8Unorm,
                              kSize, kSize);
                frame.End();
                rhi::CommandBuffer* commandBuffer = encoder->Finish();
                REQUIRE(commandBuffer != nullptr);
                rhi::CommandBuffer* commandBuffers[] = {commandBuffer};
                queue->Submit(Span<rhi::CommandBuffer* const>(commandBuffers, 1), fence, i + 1);
                REQUIRE(fence->Wait(i + 1, ~0ull));
            }

            // Target is left in CopySrc; the shared substrate does the copy + map.
            out = testsupport::Readback(device, target, kSize, kSize);

            device.WaitIdle();
            device.DestroyFence(fence);
            device.DestroyCommandPool(pool);
            device.DestroyTextureView(targetView);
            device.DestroyTexture(target);
        }
        host.Shutdown();
        return out;
    }

    u32 MaxLuma(const testsupport::CapturedImage& img)
    {
        u32 m = 0;
        for (u32 y = 0; y < img.height; ++y)
        {
            for (u32 x = 0; x < img.width; ++x)
            {
                m = Max(m, img.Luma(x, y));
            }
        }
        return m;
    }

    // Partial-coverage edge pixels: luma strictly BETWEEN background (black) and the solid cube tone.
    // The band is relative to the image's own max luma (the flat-lit cube's tonemapped value), so it
    // isolates the silhouette FRINGE regardless of what the tonemap maps "white" to - the solid cube
    // (near max) and the background (near 0) are both excluded.
    u32 CountEdgeFringe(const testsupport::CapturedImage& img, u32 maxLuma)
    {
        const u32 lo = maxLuma * 15u / 100u;
        const u32 hi = maxLuma * 85u / 100u;
        return img.CountWhere(
            [lo, hi](const u8* p) -> bool
            {
                const u32 luma = static_cast<u32>(p[0]) + p[1] + p[2];
                return luma > lo && luma < hi;
            });
    }

    void ProbeDevice(rhi::Device& device, const char* backendName)
    {
        const u32 ceiling = device.MaxColorDepthSampleCount();
        if (ceiling < 4 || !device.SupportsSampleCount(4))
        {
            MESSAGE("device has no 4x MSAA - scene-pass probe skipped on ", backendName);
            return;
        }
        const testsupport::CapturedImage at1x = RenderMsaa(device, 1);
        const testsupport::CapturedImage at4x = RenderMsaa(device, 4);
        REQUIRE(at1x.valid);
        REQUIRE(at4x.valid);

        const u32 max1x = MaxLuma(at1x);
        const u32 max4x = MaxLuma(at4x);
        const u32 fringe1x = CountEdgeFringe(at1x, max1x);
        const u32 fringe4x = CountEdgeFringe(at4x, max4x);
        std::printf("[msaa] %-8s maxLuma 1x=%u 4x=%u | fringe 1x=%u 4x=%u\n", backendName, max1x,
                    max4x, fringe1x, fringe4x);
        INFO(backendName, ": maxLuma 1x=", max1x, " 4x=", max4x, " fringe 1x=", fringe1x,
             " 4x=", fringe4x);

        // The cube must actually render (a clearly-lit solid) at both counts.
        CHECK(max1x > 300);
        CHECK(max4x > 300);

        // The acceptance property: 4x fills the silhouette with partial-coverage pixels that the
        // hard-edged 1x image (each pixel fully cube or fully background) lacks.
        CHECK(fringe4x > fringe1x * 3);
        CHECK(fringe4x > 40);
    }
}

TEST_CASE("msaa: 4x scene-pass resolve produces silhouette edge coverage that 1x does not")
{
    rhi::Backend* vulkan = nullptr;
    (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, vulkan);
    rhi::Backend* webgpu = nullptr;
    (void)rhi::webgpu::CreateBackend(rhi::webgpu::WebGpuBackendDesc{}, webgpu);

    bool any = false;
    if (rhi::Device* device = testsupport::MakeTestDevice(vulkan))
    {
        ProbeDevice(*device, "vulkan");
        device->Destroy();
        any = true;
    }
    else
    {
        MESSAGE("Vulkan unavailable - vulkan MSAA probe skipped");
    }
    if (rhi::Device* device = testsupport::MakeTestDevice(webgpu))
    {
        ProbeDevice(*device, "webgpu");
        device->Destroy();
        any = true;
    }
    else
    {
        MESSAGE("WebGPU unavailable - webgpu MSAA probe skipped");
    }
    if (!any)
    {
        MESSAGE("no GPU backend available - MSAA probe skipped entirely");
    }
}
