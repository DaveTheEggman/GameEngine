// Terrain pixel-level ground truth on a REAL Vulkan device. Renders terrain through the full
// RenderFrame chain, reads pixels back, and asserts real render outcomes the Null test can't:
//   1. a lit dome COVERS the view + SHADES (normals + lit path work, not a flat fill);
//   2. the scene's directional sun DRIVES the shading (flipping it inverts the asymmetry);
//   3. LOD-seam SKIRTS plug see-through cracks (a skirtless mixed-LOD frame leaks the background).
// Compilation is proven by the Null test; THIS proves correct pixels. Skips with no Vulkan GPU.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

#include <cstdio>

import foundation.core;
import foundation.rhi;
import foundation.rhi.vulkan;
import foundation.rhi.webgpu; // desktop backend too, not just web
#ifdef OPTION_HAS_DX12
import foundation.rhi.dx12; // Windows only
#endif
import foundation.rhi.testsupport;
import foundation.shaders.system;
import foundation.render;
import foundation.heightfield;
import foundation.terrain;
import engine.terrain;

using namespace foundation::core;
using namespace foundation::render;
namespace rhi = foundation::rhi;
namespace testsupport = foundation::rhi::testsupport;
namespace shaders = foundation::shaders;
namespace hf = foundation::heightfield;
namespace tmodel = foundation::terrain;

namespace
{
    constexpr u32 kSize = 256;

    // A radial dome: 1.0 at the centre, 0 at the rim (varying normals -> varying shading).
    RefPtr<hf::Heightfield> MakeDome()
    {
        constexpr i32 n = 129;
        RefPtr<hf::Heightfield> h =
            MakeRef<hf::Heightfield>(DefaultAllocator(), n, Float2{130.0f, 130.0f}, 0.0f, 30.0f);
        const f32 c = static_cast<f32>(n - 1) * 0.5f;
        for (i32 z = 0; z < n; ++z)
        {
            for (i32 x = 0; x < n; ++x)
            {
                const f32 dx = (static_cast<f32>(x) - c) / c;
                const f32 dz = (static_cast<f32>(z) - c) / c;
                const f32 r = Min(Sqrt(dx * dx + dz * dz), 1.0f);
                const f32 hgt = Cos(r * 3.14159265f) * 0.5f + 0.5f; // 1 centre .. 0 rim
                h->SetSample(x, z, static_cast<hf::Height>(hgt * 65535.0f));
            }
        }
        return h;
    }

    struct ProbeCfg
    {
        RefPtr<hf::Heightfield> terrain;
        Float3 eye{0, 120, 0.001f};
        Float3 target{0, 0, 0};
        Float3 up{0, 0, 1};
        f32 fov = 1.0f;
        rhi::ClearColor clear = rhi::ClearColor::Black();
        bool skirts = true;
        const Float3* toLight = nullptr;    // dir TO the light; null = renderer fallback sun
        const f32* thresholds = nullptr;    // LOD coverage thresholds override (null = default set)
        u32 thresholdCount = 0;
    };

    struct Probe
    {
        bool valid = false;
        u32 filled = 0; // pixels brighter than the black background
        f64 leftLuma = 0, rightLuma = 0, topLuma = 0, bottomLuma = 0, total = 0;
    };

    Probe RenderTerrainProbe(rhi::Device& device, const ProbeCfg& cfg)
    {
        Probe probe;
        shaders::ShaderSystemHost host;
        if (!host.Initialize(device, StringView(reinterpret_cast<const char8_t*>(
                                          BUILTIN_ENGINE_SHADER_DIR))))
        {
            return probe;
        }
        {
            shaders::ShaderSystem& shaderSystem = *host.System();
            engine::terrain::TerrainRenderer renderer(device, shaderSystem, /*framesInFlight*/ 2);
            REQUIRE(renderer.Initialize().IsOk());
            renderer.SetSkirtsEnabled(cfg.skirts);
            RendererRegistry registry;
            registry.Register(&renderer);
            RenderFrame frame(device, registry, /*framesInFlight*/ 2);

            const hf::Heightfield& terrain = *cfg.terrain;
            Array<tmodel::TerrainChunk> chunks;
            tmodel::BuildChunks(terrain, chunks);
            tmodel::TerrainQuadtree tree;
            tree.Build(Span<const tmodel::TerrainChunk>{chunks.Data(), chunks.Size()},
                       tmodel::ChunksPerSide(terrain.Size()));
            engine::terrain::TerrainHeightTextureCache heightCache;
            rhi::TextureView* heightView = heightCache.GetOrCreate(device, terrain, 1);
            REQUIRE(heightView != nullptr);

            static const f32 defaultThresholds[] = {1.0f, 0.25f, 0.08f, 0.03f, 0.012f, 0.005f, 0.002f};
            const f32* thresholds = cfg.thresholds != nullptr ? cfg.thresholds : defaultThresholds;
            const u32 thresholdCount = cfg.thresholds != nullptr ? cfg.thresholdCount : 7u;
            ExtractedScene scene;
            scene.SetAmbient(Float3{1.0f, 1.0f, 1.0f});
            if (cfg.toLight != nullptr)
            {
                GpuLight sun{};
                sun.type = 0.0f; // directional
                sun.directionWS = *cfg.toLight * -1.0f;
                sun.color = Float3{1.0f, 1.0f, 1.0f};
                sun.intensity = 1.0f;
                scene.AddLight(sun);
            }
            engine::terrain::TerrainRenderData* rd = scene.Add<engine::terrain::TerrainRenderData>();
            REQUIRE(rd != nullptr);
            rd->category = RenderCategories::Opaque;
            rd->rendererId = renderer.RendererId();
            rd->chunks = chunks.Data();
            rd->quadtree = &tree;
            rd->chunkCount = static_cast<u32>(chunks.Size());
            rd->heightView = heightView;
            rd->chunkToWorld = Float4x4::Identity();
            rd->gridSize = terrain.Size();
            rd->worldSizeXZ = terrain.WorldSize();
            rd->minY = terrain.MinY();
            rd->maxY = terrain.MaxY();
            for (u32 i = 0; i < thresholdCount; ++i)
            {
                rd->thresholds[i] = thresholds[i];
            }
            rd->thresholdCount = thresholdCount;
            rd->worldCenter = Float3{0.0f, 0.5f * (terrain.MaxY() + terrain.MinY()), 0.0f};
            rd->worldRadius = Length(terrain.WorldSize()) + (terrain.MaxY() - terrain.MinY());

            ViewCamera camera;
            camera.view = Float4x4::LookAtRH(cfg.eye, cfg.target, cfg.up);
            camera.projection = Float4x4::PerspectiveFovRH(cfg.fov, 1.0f, 1.0f, 4000.0f);

            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA8Unorm;
            td.width = kSize;
            td.height = kSize;
            td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
            td.label = u8"terrain.probe.target";
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
            settings.clear = cfg.clear;
            settings.targetTexture = target;
            settings.targetFinalState = rhi::ResourceState::CopySrc;
            settings.post.bloomEnabled = false;

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

            const testsupport::CapturedImage img =
                testsupport::Readback(device, target, kSize, kSize);
            REQUIRE(img.valid);
            for (u32 y = 0; y < kSize; ++y)
            {
                for (u32 x = 0; x < kSize; ++x)
                {
                    const u8* p = img.At(x, y);
                    const u32 luma = static_cast<u32>(p[0]) + p[1] + p[2];
                    probe.total += luma;
                    if (luma > 30)
                    {
                        ++probe.filled;
                    }
                    (x < kSize / 2 ? probe.leftLuma : probe.rightLuma) += luma;
                    (y < kSize / 2 ? probe.topLuma : probe.bottomLuma) += luma;
                }
            }
            probe.valid = true;

            device.WaitIdle();
            device.DestroyFence(fence);
            device.DestroyCommandPool(pool);
            device.DestroyTextureView(targetView);
            device.DestroyTexture(target);
        }
        host.Shutdown();
        return probe;
    }

    rhi::Device* MakeVulkan(rhi::Backend*& backendOut)
    {
        backendOut = nullptr;
        (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, backendOut);
        return backendOut != nullptr ? testsupport::MakeTestDevice(backendOut) : nullptr;
    }

    // Render the dome on a backend and return its probe; {valid=false} if no device (skip). Owns the
    // device (created + destroyed here); the caller owns the backend.
    Probe DomeProbeOn(rhi::Backend* backend)
    {
        rhi::Device* device = backend != nullptr ? testsupport::MakeTestDevice(backend) : nullptr;
        if (device == nullptr)
        {
            return Probe{};
        }
        ProbeCfg cfg;
        cfg.terrain = MakeDome();
        const Probe probe = RenderTerrainProbe(*device, cfg);
        device->Destroy();
        return probe;
    }

    // Require a backend's dome to match the Vulkan reference (a shader-cook divergence in the height
    // fetch or the normal/lit path shows up as a coverage or luma mismatch).
    void CheckMatchesReference(const char* name, const Probe& ref, const Probe& p)
    {
        std::printf("[terrain-%s] filled=%u total=%.0f (ref filled=%u total=%.0f)\n", name, p.filled,
                    p.total, ref.filled, ref.total);
        CHECK(p.filled > 32000u); // it rendered terrain, not a black/failed frame
        CHECK(static_cast<f64>(p.filled) == doctest::Approx(static_cast<f64>(ref.filled)).epsilon(0.02));
        CHECK(p.total == doctest::Approx(ref.total).epsilon(0.05));
        CHECK(p.leftLuma == doctest::Approx(ref.leftLuma).epsilon(0.05));
        CHECK(p.bottomLuma == doctest::Approx(ref.bottomLuma).epsilon(0.05));
    }
}

TEST_CASE("terrain probe: a lit dome renders + shades on Vulkan")
{
    rhi::Backend* vulkan = nullptr;
    rhi::Device* device = MakeVulkan(vulkan);
    if (device == nullptr)
    {
        MESSAGE("Vulkan unavailable - terrain probe skipped");
        if (vulkan != nullptr)
        {
            vulkan->Destroy();
        }
        return;
    }

    ProbeCfg cfg;
    cfg.terrain = MakeDome();
    const Probe p = RenderTerrainProbe(*device, cfg);
    REQUIRE(p.valid);
    const f64 pixels = static_cast<f64>(kSize) * kSize;
    std::printf("[terrain-probe] filled=%u/%.0f left=%.0f right=%.0f top=%.0f bottom=%.0f\n",
                p.filled, pixels, p.leftLuma, p.rightLuma, p.topLuma, p.bottomLuma);

    CHECK(static_cast<f64>(p.filled) > pixels * 0.5); // it rendered + covers the view
    const f64 axisAsymmetry = Abs(p.leftLuma - p.rightLuma) + Abs(p.topLuma - p.bottomLuma);
    CHECK(axisAsymmetry > p.total * 0.02); // directional shading, not a flat fill

    device->Destroy();
    vulkan->Destroy();
}

TEST_CASE("terrain probe: the scene's directional sun drives the shading (flip inverts it)")
{
    rhi::Backend* vulkan = nullptr;
    rhi::Device* device = MakeVulkan(vulkan);
    if (device == nullptr)
    {
        MESSAGE("Vulkan unavailable - terrain sun probe skipped");
        if (vulkan != nullptr)
        {
            vulkan->Destroy();
        }
        return;
    }

    const Float3 toLightPlusX = Normalized(Float3{0.85f, 0.5f, 0.0f});
    const Float3 toLightMinusX = Normalized(Float3{-0.85f, 0.5f, 0.0f});
    ProbeCfg cfg;
    cfg.terrain = MakeDome();
    cfg.toLight = &toLightPlusX;
    const Probe a = RenderTerrainProbe(*device, cfg);
    cfg.toLight = &toLightMinusX;
    const Probe b = RenderTerrainProbe(*device, cfg);
    REQUIRE(a.valid);
    REQUIRE(b.valid);

    const f64 da = a.leftLuma - a.rightLuma;
    const f64 db = b.leftLuma - b.rightLuma;
    std::printf("[terrain-sun] +X: L-R=%.0f   -X: L-R=%.0f\n", da, db);
    CHECK(da * db < 0.0);            // asymmetry inverted with the sun
    CHECK(Abs(da) > a.total * 0.02); // each a real, sizable asymmetry
    CHECK(Abs(db) > b.total * 0.02);

    device->Destroy();
    vulkan->Destroy();
}

TEST_CASE("terrain probe: every backend matches Vulkan (cross-backend shader-cook parity)")
{
    // Vulkan is the reference; every other backend we support must render the SAME dome pixel-for-
    // pixel (within driver rounding). This is where a shader-cook divergence surfaces: the integer
    // Load on the R16Uint height texture (the load-bearing WebGPU/WGSL portability bet), the GBuffer
    // MRT layout, or the normal/lit path differing between the SPIR-V, WGSL (WebGPU, desktop + web),
    // and DXIL (DX12) frontends. GPUs are checked at runtime; each backend skips cleanly if absent.
    rhi::Backend* vulkan = nullptr;
    (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, vulkan);
    rhi::Backend* webgpu = nullptr;
    (void)rhi::webgpu::CreateBackend(rhi::webgpu::WebGpuBackendDesc{}, webgpu);
#ifdef OPTION_HAS_DX12
    rhi::Backend* dx12 = nullptr;
    (void)rhi::dx12::CreateDxBackend(rhi::dx12::DxBackendDesc{}, dx12);
#endif

    const Probe reference = DomeProbeOn(vulkan);
    if (!reference.valid)
    {
        MESSAGE("Vulkan unavailable - cross-backend terrain probe skipped");
    }
    else
    {
        if (const Probe w = DomeProbeOn(webgpu); w.valid)
        {
            CheckMatchesReference("webgpu", reference, w);
        }
        else
        {
            MESSAGE("WebGPU unavailable - skipped");
        }
#ifdef OPTION_HAS_DX12
        if (const Probe d = DomeProbeOn(dx12); d.valid)
        {
            CheckMatchesReference("dx12", reference, d);
        }
        else
        {
            MESSAGE("DX12 unavailable - skipped");
        }
#endif
    }

    if (vulkan != nullptr) { vulkan->Destroy(); }
    if (webgpu != nullptr) { webgpu->Destroy(); }
#ifdef OPTION_HAS_DX12
    if (dx12 != nullptr) { dx12->Destroy(); }
#endif
}
