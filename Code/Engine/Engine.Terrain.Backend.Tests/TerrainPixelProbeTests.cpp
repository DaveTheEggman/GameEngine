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
import foundation.texture.resource; // texture::Texture (Adopt) for in-memory splat/albedo fixtures
import foundation.terrain.resource;  // Splatmap + PaintWeight (the paint -> re-upload path)
import engine.terrain;

using namespace foundation::core;
using namespace foundation::render;
namespace rhi = foundation::rhi;
namespace testsupport = foundation::rhi::testsupport;
namespace shaders = foundation::shaders;
namespace hf = foundation::heightfield;
namespace tmodel = foundation::terrain;
namespace texture = foundation::texture;

namespace
{
    constexpr u32 kSize = 256;

    // A radial dome: 1.0 at the center, 0 at the rim (varying normals -> varying shading).
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
                const f32 hgt = Cos(r * 3.14159265f) * 0.5f + 0.5f; // 1 center .. 0 rim
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
        // D2 splat material (null / 0 = layerless -> height-lit fallback).
        rhi::TextureView* splatmapView = nullptr;
        rhi::TextureView* albedoViews[4] = {nullptr, nullptr, nullptr, nullptr};
        f32 tileScales[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        u32 layerCount = 0;
    };

    struct Probe
    {
        bool valid = false;
        u32 filled = 0; // pixels brighter than the black background
        f64 leftLuma = 0, rightLuma = 0, topLuma = 0, bottomLuma = 0, total = 0;
        f64 leftR = 0, leftB = 0, rightR = 0, rightB = 0; // per-channel bands (splat colour check)
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
            rd->splatmapView = cfg.splatmapView;
            for (u32 li = 0; li < 4; ++li)
            {
                rd->albedoViews[li] = cfg.albedoViews[li];
                rd->tileScales[li] = cfg.tileScales[li];
            }
            rd->layerCount = cfg.layerCount;
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
                    if (x < kSize / 2) { probe.leftR += p[0]; probe.leftB += p[2]; }
                    else { probe.rightR += p[0]; probe.rightB += p[2]; }
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

    // Flat ground + a tall thin N-S ridge at the center X. A low +X sun makes the ridge cast a long
    // shadow across the flat -X ground - a clean caster/receiver: the flat ground has a uniform normal,
    // so any left/right darkening there is PURELY the cast shadow, not n.l shading.
    RefPtr<hf::Heightfield> MakeRidge()
    {
        constexpr i32 n = 257;
        RefPtr<hf::Heightfield> h =
            MakeRef<hf::Heightfield>(DefaultAllocator(), n, Float2{256.0f, 256.0f}, 0.0f, 60.0f);
        const i32 c = (n - 1) / 2;
        for (i32 z = 0; z < n; ++z)
        {
            for (i32 x = 0; x < n; ++x)
            {
                const bool wall = (x >= c - 3 && x <= c + 3); // a few cells wide, spanning Z
                h->SetSample(x, z, static_cast<hf::Height>((wall ? 0.92f : 0.05f) * 65535.0f));
            }
        }
        return h;
    }

    // A flat terrain (constant mid height) - a clean canvas for the splat colour check.
    RefPtr<hf::Heightfield> MakeFlat()
    {
        constexpr i32 n = 129;
        RefPtr<hf::Heightfield> h =
            MakeRef<hf::Heightfield>(DefaultAllocator(), n, Float2{130.0f, 130.0f}, 0.0f, 30.0f);
        for (i32 z = 0; z < n; ++z)
        {
            for (i32 x = 0; x < n; ++x)
            {
                h->SetSample(x, z, static_cast<hf::Height>(0.3f * 65535.0f));
            }
        }
        return h;
    }

    // Wrap an RGBA8 texture (uploaded from `pixels`, w*h*4 bytes) as an in-memory texture::Texture
    // (Adopt owns + frees the GPU objects on the returned RefPtr's destruction).
    RefPtr<texture::Texture> MakeRGBA(rhi::Device& device, u32 w, u32 h, const u8* pixels)
    {
        rhi::TextureDesc td{};
        td.format = rhi::TextureFormat::RGBA8Unorm;
        td.width = w;
        td.height = h;
        td.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst;
        td.label = u8"probe.albedo";
        rhi::Texture* tex = nullptr;
        REQUIRE(device.CreateTexture(td, tex).IsOk());
        rhi::TextureViewDesc vd{};
        vd.format = rhi::TextureFormat::RGBA8Unorm;
        vd.dimension = rhi::TextureViewDimension::Texture2D;
        rhi::TextureView* view = nullptr;
        REQUIRE(device.CreateTextureView(tex, vd, view).IsOk());
        if (rhi::Queue* q = device.GetQueue(rhi::QueueType::Graphics))
        {
            rhi::TransferBatch* tb = nullptr;
            if (q->CreateTransferBatch(tb).IsOk() && tb != nullptr)
            {
                rhi::TextureDataLayout layout{};
                layout.bytesPerRow = w * 4;
                layout.rowsPerImage = h;
                tb->WriteTexture(tex, Span<const u8>{pixels, static_cast<usize>(w) * h * 4}, layout,
                                 rhi::Extent3D{w, h, 1});
                (void)tb->Submit();
                q->DestroyTransferBatch(tb);
            }
        }
        RefPtr<texture::Texture> t = MakeRef<texture::Texture>(DefaultAllocator());
        t->Adopt(&device, tex, view, nullptr, w, h, rhi::TextureFormat::RGBA8Unorm, false);
        return t;
    }

    RefPtr<texture::Texture> MakeSolid(rhi::Device& device, u8 r, u8 g, u8 b)
    {
        const u8 px[4] = {r, g, b, 255};
        return MakeRGBA(device, 1, 1, px);
    }

    // A splatmap split left/right: left half = weight on layer 0 (R), right half = layer 1 (G).
    RefPtr<texture::Texture> MakeSplitSplatmap(rhi::Device& device)
    {
        constexpr u32 n = 16;
        u8 px[n * n * 4];
        for (u32 y = 0; y < n; ++y)
        {
            for (u32 x = 0; x < n; ++x)
            {
                u8* p = px + (static_cast<usize>(y) * n + x) * 4;
                const bool left = x < n / 2;
                p[0] = left ? 255 : 0; // layer 0 weight
                p[1] = left ? 0 : 255; // layer 1 weight
                p[2] = 0;
                p[3] = 0;
            }
        }
        return MakeRGBA(device, n, n, px);
    }

    struct ShadowProbe
    {
        bool valid = false;
        f64 leftGround = 0, rightGround = 0, total = 0; // luma over the flat ground bands
    };

    // Render the ridge top-down under a low +X sun, optionally with a CSM ShadowSystem, and measure the
    // luma of the flat ground on each side of the ridge (excluding the bright ridge stripe itself).
    ShadowProbe RenderShadowProbe(rhi::Device& device, bool shadowsEnabled)
    {
        ShadowProbe probe;
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
            RendererRegistry registry;
            registry.Register(&renderer);

            UniquePtr<ShadowSystem> shadows;
            if (shadowsEnabled)
            {
                shadows = MakeUnique<ShadowSystem>(DefaultAllocator(), device, /*framesInFlight*/ 2);
                REQUIRE(shadows->Initialize().IsOk());
            }
            RenderFrame frame(device, registry, /*framesInFlight*/ 2, /*clusters*/ nullptr,
                              /*tonemap*/ nullptr, shadows.Get());
            frame.SetShadowParams(800.0f, 20.0f); // shadow distance, far-fade width

            RefPtr<hf::Heightfield> ridge = MakeRidge();
            Array<tmodel::TerrainChunk> chunks;
            tmodel::BuildChunks(*ridge, chunks);
            tmodel::TerrainQuadtree tree;
            tree.Build(Span<const tmodel::TerrainChunk>{chunks.Data(), chunks.Size()},
                       tmodel::ChunksPerSide(ridge->Size()));
            engine::terrain::TerrainHeightTextureCache heightCache;
            rhi::TextureView* heightView = heightCache.GetOrCreate(device, *ridge, 1);
            REQUIRE(heightView != nullptr);

            // Sun low from +X: direction TO light, then travel = -that.
            const Float3 toLight = Normalized(Float3{0.9f, 0.42f, 0.0f});
            const Float3 travel = toLight * -1.0f;

            static const f32 thresholds[] = {1.0f, 0.25f, 0.08f, 0.03f, 0.012f, 0.005f, 0.002f};
            ExtractedScene scene;
            scene.SetAmbient(Float3{1.0f, 1.0f, 1.0f});
            GpuLight sun{};
            sun.type = 0.0f;
            sun.directionWS = travel;
            sun.color = Float3{1.0f, 1.0f, 1.0f};
            sun.intensity = 1.0f;
            scene.AddLight(sun);
            DirectionalShadow ds{};
            ds.direction = travel;
            ds.valid = true;
            scene.SetDirectionalShadow(ds);

            engine::terrain::TerrainRenderData* rd = scene.Add<engine::terrain::TerrainRenderData>();
            REQUIRE(rd != nullptr);
            rd->category = RenderCategories::Opaque;
            rd->rendererId = renderer.RendererId();
            rd->chunks = chunks.Data();
            rd->quadtree = &tree;
            rd->chunkCount = static_cast<u32>(chunks.Size());
            rd->heightView = heightView;
            rd->chunkToWorld = Float4x4::Identity();
            rd->gridSize = ridge->Size();
            rd->worldSizeXZ = ridge->WorldSize();
            rd->minY = ridge->MinY();
            rd->maxY = ridge->MaxY();
            for (u32 i = 0; i < 7; ++i)
            {
                rd->thresholds[i] = thresholds[i];
            }
            rd->thresholdCount = 7;
            rd->worldCenter = Float3{0.0f, 30.0f, 0.0f};
            rd->worldRadius = 400.0f;

            ViewCamera camera;
            camera.view = Float4x4::LookAtRH(Float3{0.0f, 260.0f, 0.01f}, Float3{0.0f, 0.0f, 0.0f},
                                             Float3{0.0f, 0.0f, 1.0f});
            camera.projection = Float4x4::PerspectiveFovRH(1.0f, 1.0f, 1.0f, 900.0f);

            rhi::TextureDesc td{};
            td.format = rhi::TextureFormat::RGBA8Unorm;
            td.width = kSize;
            td.height = kSize;
            td.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc;
            td.label = u8"terrain.shadowprobe";
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
            // Two flat-ground bands well clear of the center ridge stripe.
            for (u32 y = 0; y < kSize; ++y)
            {
                for (u32 x = 0; x < kSize; ++x)
                {
                    const f64 luma = static_cast<f64>(img.Luma(x, y));
                    probe.total += luma;
                    const f32 fx = static_cast<f32>(x) / kSize;
                    if (fx > 0.12f && fx < 0.38f)
                    {
                        probe.leftGround += luma;
                    }
                    else if (fx > 0.62f && fx < 0.88f)
                    {
                        probe.rightGround += luma;
                    }
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

TEST_CASE("terrain probe: the ridge casts a CSM shadow onto the flat ground (cast + receive)")
{
    rhi::Backend* vulkan = nullptr;
    rhi::Device* device = MakeVulkan(vulkan);
    if (device == nullptr)
    {
        MESSAGE("Vulkan unavailable - terrain shadow probe skipped");
        if (vulkan != nullptr) { vulkan->Destroy(); }
        return;
    }

    // Same scene, shadows off then on. The flat ground has ONE normal, so without shadows both bands
    // are equally lit; with the CSM the ridge darkens one side (its cast shadow). Terrain must both
    // CAST (into the cascade) and RECEIVE (sample it) for this to appear.
    const ShadowProbe off = RenderShadowProbe(*device, /*shadowsEnabled*/ false);
    const ShadowProbe on = RenderShadowProbe(*device, /*shadowsEnabled*/ true);
    REQUIRE(off.valid);
    REQUIRE(on.valid);

    const f64 offAsym = Abs(off.leftGround - off.rightGround) / (off.leftGround + off.rightGround);
    const f64 onAsym = Abs(on.leftGround - on.rightGround) / (on.leftGround + on.rightGround);
    std::printf("[terrain-shadow] off L=%.0f R=%.0f (asym %.3f) | on L=%.0f R=%.0f (asym %.3f) "
                "| total off=%.0f on=%.0f\n",
                off.leftGround, off.rightGround, offAsym, on.leftGround, on.rightGround, onAsym,
                off.total, on.total);

    CHECK(offAsym < 0.06);              // flat ground, no shadow -> both sides ~equal
    CHECK(onAsym > 0.15);               // the cast shadow darkens one side
    CHECK(on.total < off.total * 0.99); // shadows only remove light

    device->Destroy();
    vulkan->Destroy();
}

TEST_CASE("terrain probe: splat blends layer albedos (D2), matching across backends")
{
    // Flat terrain, a split splatmap (left = layer 0, right = layer 1), layer 0 = red, layer 1 = blue.
    // The two halves must show the two albedos (proving splat selection + blend), pixel-exact on both
    // backends. Textures are created per-device and freed before the device (Adopt owns them).
    auto run = [](rhi::Backend* backend) -> Probe
    {
        rhi::Device* dev = (backend != nullptr) ? testsupport::MakeTestDevice(backend) : nullptr;
        if (dev == nullptr)
        {
            return Probe{};
        }
        Probe p;
        {
            RefPtr<texture::Texture> red = MakeSolid(*dev, 220, 30, 30);
            RefPtr<texture::Texture> blue = MakeSolid(*dev, 30, 30, 220);
            RefPtr<texture::Texture> splat = MakeSplitSplatmap(*dev);
            ProbeCfg cfg;
            cfg.terrain = MakeFlat();
            cfg.splatmapView = splat->View();
            cfg.albedoViews[0] = red->View();
            cfg.albedoViews[1] = blue->View();
            cfg.tileScales[0] = cfg.tileScales[1] = 1000.0f; // ~1 tile -> solid colour across the terrain
            cfg.layerCount = 2;
            p = RenderTerrainProbe(*dev, cfg);
        }
        dev->Destroy();
        return p;
    };

    rhi::Backend* vulkan = nullptr;
    (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, vulkan);
    rhi::Backend* webgpu = nullptr;
    (void)rhi::webgpu::CreateBackend(rhi::webgpu::WebGpuBackendDesc{}, webgpu);

    const Probe v = run(vulkan);
    if (!v.valid)
    {
        MESSAGE("Vulkan unavailable - terrain splat probe skipped");
        if (vulkan != nullptr) { vulkan->Destroy(); }
        if (webgpu != nullptr) { webgpu->Destroy(); }
        return;
    }
    std::printf("[terrain-splat] vk left(R=%.0f B=%.0f) right(R=%.0f B=%.0f)\n", v.leftR, v.leftB,
                v.rightR, v.rightB);

    // The two layers landed on opposite screen halves (flip-agnostic: one half red-dominant, the
    // other blue-dominant). Proves the splatmap selected different albedos per region.
    const bool redLeft = v.leftR > v.leftB * 1.5 && v.rightB > v.rightR * 1.5;
    const bool blueLeft = v.leftB > v.leftR * 1.5 && v.rightR > v.rightB * 1.5;
    CHECK((redLeft || blueLeft));

    const Probe w = run(webgpu);
    if (w.valid)
    {
        std::printf("[terrain-splat] wg left(R=%.0f B=%.0f) right(R=%.0f B=%.0f)\n", w.leftR,
                    w.leftB, w.rightR, w.rightB);
        CHECK(w.leftR == doctest::Approx(v.leftR).epsilon(0.05));
        CHECK(w.leftB == doctest::Approx(v.leftB).epsilon(0.05));
        CHECK(w.rightR == doctest::Approx(v.rightR).epsilon(0.05));
        CHECK(w.rightB == doctest::Approx(v.rightB).epsilon(0.05));
    }
    else
    {
        MESSAGE("WebGPU unavailable - splat cross-check skipped");
    }

    if (vulkan != nullptr) { vulkan->Destroy(); }
    if (webgpu != nullptr) { webgpu->Destroy(); }
}

TEST_CASE("terrain probe: painting the CPU splatmap re-uploads and changes the blended pixel")
{
    // The editor Splat Paint loop end-to-end: a CPU Splatmap -> the version-keyed GPU splat cache ->
    // the shader blend. Seeded to layer 0 (red) it renders red; PaintWeight fills layer 1 (blue) and
    // bumps the version, the cache re-uploads a NEW texture, and the SAME terrain now renders blue -
    // proving the paint reached the GPU. Vulkan AND WebGPU (validate-on-WebGPU rule).
    namespace terrain = foundation::terrain;
    auto run = [](rhi::Backend* backend, Probe& before, Probe& after)
    {
        rhi::Device* dev = (backend != nullptr) ? testsupport::MakeTestDevice(backend) : nullptr;
        if (dev == nullptr)
        {
            return;
        }
        {
            engine::terrain::TerrainSplatTextureCache cache;
            RefPtr<terrain::Splatmap> sm = MakeRef<terrain::Splatmap>(DefaultAllocator(), 8, 8);
            sm->SeedLayer0(); // all weight on layer 0

            RefPtr<texture::Texture> red = MakeSolid(*dev, 220, 30, 30);  // layer 0
            RefPtr<texture::Texture> blue = MakeSolid(*dev, 30, 30, 220); // layer 1
            ProbeCfg cfg;
            cfg.terrain = MakeFlat();
            cfg.albedoViews[0] = red->View();
            cfg.albedoViews[1] = blue->View();
            cfg.tileScales[0] = cfg.tileScales[1] = 1000.0f; // ~1 tile -> solid colour
            cfg.layerCount = 2;

            // BEFORE: the cache uploads the seeded raster (layer 0) -> red across the terrain.
            cfg.splatmapView = cache.GetOrCreate(*dev, *sm, sm->Version());
            before = RenderTerrainProbe(*dev, cfg);

            // PAINT layer 1 over the whole footprint (a few dabs to converge one-hot), then re-fetch:
            // the version bump makes the cache rebuild + re-upload a new view.
            for (i32 i = 0; i < 4; ++i)
            {
                (void)terrain::PaintWeight(*sm, 0.5f, 0.5f, 2.0f, 1u, 1.0f);
            }
            cfg.splatmapView = cache.GetOrCreate(*dev, *sm, sm->Version());
            after = RenderTerrainProbe(*dev, cfg);

            cache.Clear(*dev);
        }
        dev->Destroy();
    };

    rhi::Backend* vulkan = nullptr;
    (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, vulkan);
    rhi::Backend* webgpu = nullptr;
    (void)rhi::webgpu::CreateBackend(rhi::webgpu::WebGpuBackendDesc{}, webgpu);

    Probe vBefore, vAfter;
    run(vulkan, vBefore, vAfter);
    if (!vBefore.valid || !vAfter.valid)
    {
        MESSAGE("Vulkan unavailable - splat re-upload probe skipped");
        if (vulkan != nullptr) { vulkan->Destroy(); }
        if (webgpu != nullptr) { webgpu->Destroy(); }
        return;
    }
    std::printf("[terrain-splat-repaint] vk before(R=%.0f B=%.0f) after(R=%.0f B=%.0f)\n",
                vBefore.leftR, vBefore.leftB, vAfter.leftR, vAfter.leftB);

    // Seeded -> red-dominant; after painting layer 1 -> blue-dominant. The blend changed = the paint
    // re-uploaded to the GPU.
    CHECK(vBefore.leftR > vBefore.leftB * 1.5);
    CHECK(vBefore.rightR > vBefore.rightB * 1.5);
    CHECK(vAfter.leftB > vAfter.leftR * 1.5);
    CHECK(vAfter.rightB > vAfter.rightR * 1.5);

    Probe wBefore, wAfter;
    run(webgpu, wBefore, wAfter);
    if (wBefore.valid && wAfter.valid)
    {
        std::printf("[terrain-splat-repaint] wg before(R=%.0f B=%.0f) after(R=%.0f B=%.0f)\n",
                    wBefore.leftR, wBefore.leftB, wAfter.leftR, wAfter.leftB);
        CHECK(wBefore.leftR == doctest::Approx(vBefore.leftR).epsilon(0.05));
        CHECK(wAfter.leftB == doctest::Approx(vAfter.leftB).epsilon(0.05));
    }
    else
    {
        MESSAGE("WebGPU unavailable - splat re-upload cross-check skipped");
    }

    if (vulkan != nullptr) { vulkan->Destroy(); }
    if (webgpu != nullptr) { webgpu->Destroy(); }
}
