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
import foundation.texture.resource; // texture::Texture (Adopt) for in-memory albedo fixtures
import foundation.terrain.resource; // SplatWeights + PaintTopK + TerrainPaletteData (top-K model)
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
        // Top-K splat material (all null / 0 = no material -> height-lit fallback).
        rhi::TextureView* weightView = nullptr;     // RGBA8Unorm slot weights
        rhi::TextureView* indexView = nullptr;      // RGBA8Uint palette indices
        rhi::TextureView* baseAlbedoView = nullptr; // the BASE layer (erase reveals)
        rhi::TextureView* baseNormalView = nullptr; // BASE tangent-space normal map (terrain PBR)
        rhi::TextureView* baseOrmView = nullptr;    // BASE ORM (R=AO G=rough B=metal)
        f32 baseTileScale = 1.0f;
        rhi::TextureView* paletteArrayView = nullptr; // Texture2DArray, one slice per layer
        rhi::TextureView* normalArrayView = nullptr;  // per-layer normal array (terrain PBR)
        rhi::TextureView* ormArrayView = nullptr;     // per-layer ORM array
        Float4x4 chunkToWorld = Float4x4::Identity(); // R2: the tangent frame follows THIS
        rhi::Buffer* tileScaleBuffer = nullptr;       // f32[paletteCount]
        u64 tileScaleGeneration = 0;
        u32 paletteCount = 0;
    };

    constexpr u32 kBands = 6; // vertical screen bands for the stripe-fixture colour readout

    struct Probe
    {
        bool valid = false;
        u32 filled = 0; // pixels brighter than the black background
        f64 leftLuma = 0, rightLuma = 0, topLuma = 0, bottomLuma = 0, total = 0;
        f64 leftR = 0, leftB = 0, rightR = 0, rightB = 0; // per-channel halves (colour checks)
        // Band-centre channel means (x split into kBands, centre half of each band, centre half
        // in y - clear of band boundaries and the terrain rim) for the >4-layer stripe fixture.
        f64 bandR[kBands] = {};
        f64 bandG[kBands] = {};
        f64 bandB[kBands] = {};
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
            rd->nodes = tree.Nodes().Data();
            rd->nodeCount = static_cast<u32>(tree.Nodes().Size());rd->chunkCount = static_cast<u32>(chunks.Size());
            rd->heightView = heightView;
            rd->chunkToWorld = cfg.chunkToWorld;
            rd->gridSize = terrain.Size();
            rd->worldSizeXZ = terrain.WorldSize();
            rd->minY = terrain.MinY();
            rd->maxY = terrain.MaxY();
            for (u32 i = 0; i < thresholdCount; ++i)
            {
                rd->thresholds[i] = thresholds[i];
            }
            rd->thresholdCount = thresholdCount;
            rd->weightView = cfg.weightView;
            rd->indexView = cfg.indexView;
            rd->baseAlbedoView = cfg.baseAlbedoView;
            rd->baseNormalView = cfg.baseNormalView;
            rd->baseOrmView = cfg.baseOrmView;
            rd->baseTileScale = cfg.baseTileScale;
            rd->paletteArrayView = cfg.paletteArrayView;
            rd->normalArrayView = cfg.normalArrayView;
            rd->ormArrayView = cfg.ormArrayView;
            rd->tileScaleBuffer = cfg.tileScaleBuffer;
            rd->tileScaleGeneration = cfg.tileScaleGeneration;
            rd->paletteCount = cfg.paletteCount;
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
                pool->DestroyEncoder(encoder); // fence waited: safe (ASAN pass-16 finding)
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
                    // Band-centre sums (the stripe fixture): centre half in y, centre half of
                    // each band in x - clear of stripe-boundary bilinear blends + the rim.
                    if (y >= kSize / 4 && y < kSize * 3 / 4)
                    {
                        constexpr u32 bandWidth = kSize / kBands;
                        const u32 band = Min(x / bandWidth, kBands - 1);
                        const u32 xin = x - band * bandWidth;
                        if (xin >= bandWidth / 4 && xin < bandWidth * 3 / 4)
                        {
                            probe.bandR[band] += p[0];
                            probe.bandG[band] += p[1];
                            probe.bandB[band] += p[2];
                        }
                    }
                }
            }
            probe.valid = true;

            device.WaitIdle();
            heightCache.Clear(device); // owns the height texture/view (ASAN pass-16 finding)
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

    // A CPU weights raster of `bands` full-strength vertical stripes: stripe i is one-hot on
    // PALETTE layer i. With bands > 4 this is a raster the retired fixed-4-layer model could not
    // represent at all - the R5 fixture.
    RefPtr<tmodel::SplatWeights> MakeStripeWeights(u32 bands)
    {
        constexpr i32 n = 32;
        RefPtr<tmodel::SplatWeights> sw =
            MakeRef<tmodel::SplatWeights>(DefaultAllocator(), n, n);
        Span<u8> idx = sw->Indices();
        Span<u8> wts = sw->Weights();
        for (i32 y = 0; y < n; ++y)
        {
            for (i32 x = 0; x < n; ++x)
            {
                const usize at = sw->TexelOffset(x, y);
                const u32 layer = Min(static_cast<u32>(x) * bands / n, bands - 1);
                idx[at + 0] = static_cast<u8>(layer);
                wts[at + 0] = 255; // one-hot: pure layer, no base
            }
        }
        sw->BumpVersion();
        return sw;
    }

    // A cook-shaped palette texel blob of solid-colour 4x4 single-mip slices.
    RefPtr<tmodel::TerrainPaletteData> MakePaletteData(Span<const Float3> colors)
    {
        auto data = MakeRef<tmodel::TerrainPaletteData>(DefaultAllocator());
        data->sliceSize = 4;
        data->mipCount = 1;
        data->sliceCount = static_cast<u32>(colors.Size());
        const usize sliceBytes = tmodel::TerrainPaletteData::SliceBytes(4, 1);
        data->texels.Resize(sliceBytes * colors.Size());
        for (usize s = 0; s < colors.Size(); ++s)
        {
            for (usize t = 0; t < sliceBytes / 4; ++t)
            {
                u8* p = data->texels.Data() + s * sliceBytes + t * 4;
                p[0] = static_cast<u8>(colors[s].x * 255.0f);
                p[1] = static_cast<u8>(colors[s].y * 255.0f);
                p[2] = static_cast<u8>(colors[s].z * 255.0f);
                p[3] = 255;
            }
        }
        return data;
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
            rd->nodes = tree.Nodes().Data();
            rd->nodeCount = static_cast<u32>(tree.Nodes().Size());rd->chunkCount = static_cast<u32>(chunks.Size());
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
                pool->DestroyEncoder(encoder); // fence waited: safe (ASAN pass-16 finding)
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
            heightCache.Clear(device); // owns the height texture/view (ASAN pass-16 finding)
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

TEST_CASE("terrain probe: SIX palette layers render distinct stripes (R5 - the 4-layer cap is gone)")
{
    // Flat terrain, a 6-stripe one-hot weights raster (stripe i = palette layer i) and a 6-colour
    // palette array. Six DISTINCT layers on one terrain is exactly what the retired fixed-4 model
    // could not draw; every stripe must show its own palette colour, matching across Vulkan and
    // WebGPU (the ruling-R5 pixel-parity requirement). GPU objects come through the PRODUCTION
    // caches (splat pair + palette array + tileScale buffer).
    static const Float3 kColors[kBands] = {
        Float3{0.86f, 0.12f, 0.12f}, // red
        Float3{0.12f, 0.86f, 0.12f}, // green
        Float3{0.12f, 0.12f, 0.86f}, // blue
        Float3{0.86f, 0.86f, 0.12f}, // yellow
        Float3{0.12f, 0.86f, 0.86f}, // cyan
        Float3{0.86f, 0.12f, 0.86f}, // magenta
    };

    auto run = [](rhi::Backend* backend) -> Probe
    {
        rhi::Device* dev = (backend != nullptr) ? testsupport::MakeTestDevice(backend) : nullptr;
        if (dev == nullptr)
        {
            return Probe{};
        }
        Probe p;
        {
            engine::terrain::TerrainSplatTextureCache splatCache;
            engine::terrain::TerrainPaletteTextureCache paletteCache;
            RefPtr<tmodel::SplatWeights> sw = MakeStripeWeights(kBands);
            RefPtr<tmodel::TerrainPaletteData> palette =
                MakePaletteData(Span<const Float3>{kColors, kBands});
            f32 scales[kBands];
            for (u32 i = 0; i < kBands; ++i)
            {
                scales[i] = 1000.0f; // ~1 tile -> solid colour per stripe
            }

            ProbeCfg cfg;
            cfg.terrain = MakeFlat();
            const engine::terrain::SplatTextureViews views =
                splatCache.GetOrCreate(*dev, *sw, sw->Version());
            const engine::terrain::PaletteGpu gpu =
                paletteCache.GetOrCreate(*dev, *palette, Span<const f32>{scales, kBands});
            REQUIRE(views.weightView != nullptr);
            REQUIRE(gpu.arrayView != nullptr);
            cfg.weightView = views.weightView;
            cfg.indexView = views.indexView;
            cfg.paletteArrayView = gpu.arrayView;
            cfg.tileScaleBuffer = gpu.tileScaleBuffer;
            cfg.tileScaleGeneration = gpu.generation;
            cfg.paletteCount = kBands;
            p = RenderTerrainProbe(*dev, cfg);

            splatCache.Clear(*dev);
            paletteCache.Clear(*dev);
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
        MESSAGE("Vulkan unavailable - terrain top-K stripe probe skipped");
        if (vulkan != nullptr) { vulkan->Destroy(); }
        if (webgpu != nullptr) { webgpu->Destroy(); }
        return;
    }
    for (u32 b = 0; b < kBands; ++b)
    {
        std::printf("[terrain-topk] vk band %u: R=%.0f G=%.0f B=%.0f\n", b, v.bandR[b],
                    v.bandG[b], v.bandB[b]);
    }

    // Classify each screen band by which channels are "on" (> 60% of the band max) and demand the
    // palette order - forward or mirrored (flip-agnostic like the old half test).
    const auto bandClass = [](const Probe& p, u32 b) -> u32
    {
        const f64 mx = Max(p.bandR[b], Max(p.bandG[b], p.bandB[b]));
        u32 bits = 0;
        bits |= (p.bandR[b] > 0.6 * mx) ? 1u : 0u;
        bits |= (p.bandG[b] > 0.6 * mx) ? 2u : 0u;
        bits |= (p.bandB[b] > 0.6 * mx) ? 4u : 0u;
        return bits;
    };
    const u32 expected[kBands] = {1u, 2u, 4u, 3u, 6u, 5u}; // R,G,B,R+G,G+B,R+B
    bool forward = true;
    bool mirrored = true;
    for (u32 b = 0; b < kBands; ++b)
    {
        forward = forward && (bandClass(v, b) == expected[b]);
        mirrored = mirrored && (bandClass(v, b) == expected[kBands - 1 - b]);
    }
    CHECK((forward || mirrored)); // all six DISTINCT layers landed, in palette order

    const Probe w = run(webgpu);
    if (w.valid)
    {
        for (u32 b = 0; b < kBands; ++b)
        {
            CHECK(w.bandR[b] == doctest::Approx(v.bandR[b]).epsilon(0.05));
            CHECK(w.bandG[b] == doctest::Approx(v.bandG[b]).epsilon(0.05));
            CHECK(w.bandB[b] == doctest::Approx(v.bandB[b]).epsilon(0.05));
        }
    }
    else
    {
        MESSAGE("WebGPU unavailable - top-K stripe cross-check skipped");
    }

    if (vulkan != nullptr) { vulkan->Destroy(); }
    if (webgpu != nullptr) { webgpu->Destroy(); }
}

TEST_CASE("terrain probe: painting the CPU weights re-uploads and changes the blended pixel")
{
    // The editor Splat Paint loop end-to-end: CPU SplatWeights -> the version-keyed GPU splat pair
    // -> the top-K blend. All-zero weights over a red BASE renders red; PaintTopK raises palette 0
    // (blue) and bumps the version, the cache re-uploads a NEW pair, and the SAME terrain now
    // renders blue - proving the paint (and the erase-reveals-base direction in reverse) reached
    // the GPU. Vulkan AND WebGPU (validate-on-WebGPU rule).
    auto run = [](rhi::Backend* backend, Probe& before, Probe& after)
    {
        rhi::Device* dev = (backend != nullptr) ? testsupport::MakeTestDevice(backend) : nullptr;
        if (dev == nullptr)
        {
            return;
        }
        {
            engine::terrain::TerrainSplatTextureCache cache;
            engine::terrain::TerrainPaletteTextureCache paletteCache;
            RefPtr<tmodel::SplatWeights> sw =
                MakeRef<tmodel::SplatWeights>(DefaultAllocator(), 8, 8); // all-zero = pure base

            RefPtr<texture::Texture> red = MakeSolid(*dev, 220, 30, 30); // the BASE albedo
            const Float3 blue{0.12f, 0.12f, 0.86f};                      // palette 0
            RefPtr<tmodel::TerrainPaletteData> palette =
                MakePaletteData(Span<const Float3>{&blue, 1});
            const f32 scale = 1000.0f;
            const engine::terrain::PaletteGpu gpu =
                paletteCache.GetOrCreate(*dev, *palette, Span<const f32>{&scale, 1});
            REQUIRE(gpu.arrayView != nullptr);

            ProbeCfg cfg;
            cfg.terrain = MakeFlat();
            cfg.baseAlbedoView = red->View();
            cfg.baseTileScale = 1000.0f; // ~1 tile -> solid colour
            cfg.paletteArrayView = gpu.arrayView;
            cfg.tileScaleBuffer = gpu.tileScaleBuffer;
            cfg.tileScaleGeneration = gpu.generation;
            cfg.paletteCount = 1;

            // BEFORE: all-zero weights -> the BASE (red) across the terrain.
            engine::terrain::SplatTextureViews views =
                cache.GetOrCreate(*dev, *sw, sw->Version());
            cfg.weightView = views.weightView;
            cfg.indexView = views.indexView;
            before = RenderTerrainProbe(*dev, cfg);

            // PAINT palette 0 over the whole footprint (a few dabs to converge one-hot), then
            // re-fetch: the version bump makes the cache rebuild + re-upload a new pair.
            for (i32 i = 0; i < 24; ++i)
            {
                (void)tmodel::PaintTopK(*sw, 0.5f, 0.5f, 2.0f, 2.0f, 0u, 1.0f);
            }
            views = cache.GetOrCreate(*dev, *sw, sw->Version());
            cfg.weightView = views.weightView;
            cfg.indexView = views.indexView;
            after = RenderTerrainProbe(*dev, cfg);

            cache.Clear(*dev);
            paletteCache.Clear(*dev);
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

TEST_CASE("terrain probe: a base normal map perturbs the flat-ground shading (R5 - Vk + WebGPU)")
{
    // A FLAT terrain has ONE geometric normal, so a directional sun shades it uniformly. A base
    // normal map tilted toward world +X must make the ground respond to sun direction: a +X sun
    // brightens it (vs the flat-normal control) and a -X sun darkens it. Proves the analytic tangent
    // frame (terrain-layer-pbr.md R2) + the blend perturb the normal in the correct direction, and
    // that the SampleGrad-on-array normal tap matches across Vulkan and WebGPU.
    auto run = [](rhi::Backend* backend, const Float3& toLight, bool withNormal) -> Probe
    {
        rhi::Device* dev = (backend != nullptr) ? testsupport::MakeTestDevice(backend) : nullptr;
        if (dev == nullptr)
        {
            return Probe{};
        }
        Probe p;
        {
            RefPtr<texture::Texture> albedo = MakeSolid(*dev, 170, 170, 170);
            // Tangent-space normal (0.6, 0, 0.8): under the flat-terrain frame (T=+X, up=+Y) this
            // tilts the world normal toward +X. Encode n*0.5+0.5 -> (204, 128, 229).
            RefPtr<texture::Texture> normal = MakeSolid(*dev, 204, 128, 229);
            ProbeCfg cfg;
            cfg.terrain = MakeFlat();
            cfg.toLight = &toLight;
            cfg.baseAlbedoView = albedo->View();
            cfg.baseTileScale = 1.0f;
            if (withNormal)
            {
                cfg.baseNormalView = normal->View();
            }
            p = RenderTerrainProbe(*dev, cfg);
        }
        dev->Destroy();
        return p;
    };

    const Float3 plusX = Normalized(Float3{0.85f, 0.5f, 0.0f});
    const Float3 minusX = Normalized(Float3{-0.85f, 0.5f, 0.0f});

    rhi::Backend* vulkan = nullptr;
    (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, vulkan);
    rhi::Backend* webgpu = nullptr;
    (void)rhi::webgpu::CreateBackend(rhi::webgpu::WebGpuBackendDesc{}, webgpu);

    const Probe aPlus = run(vulkan, plusX, true); // normal + aligned sun
    if (!aPlus.valid)
    {
        MESSAGE("Vulkan unavailable - terrain normal-map probe skipped");
        if (vulkan != nullptr) { vulkan->Destroy(); }
        if (webgpu != nullptr) { webgpu->Destroy(); }
        return;
    }
    const Probe aMinus = run(vulkan, minusX, true);  // normal + opposed sun
    const Probe cPlus = run(vulkan, plusX, false);   // flat control, +X
    const Probe cMinus = run(vulkan, minusX, false); // flat control, -X

    std::printf("[terrain-normal] normal +X=%.0f -X=%.0f | flat +X=%.0f -X=%.0f\n", aPlus.total,
                aMinus.total, cPlus.total, cMinus.total);

    // Flat control: +X and -X sun shade the one-normal ground ~equally.
    CHECK(cPlus.total == doctest::Approx(cMinus.total).epsilon(0.06));
    // The normal map makes it directional: aligned sun brighter, opposed sun darker, than flat.
    CHECK(aPlus.total > cPlus.total * 1.10);
    CHECK(aMinus.total < cMinus.total * 0.90);

    // WebGPU parity on the aligned case (the SampleGrad-on-array normal tap is where naga diverges).
    const Probe wPlus = run(webgpu, plusX, true);
    if (wPlus.valid)
    {
        CHECK(wPlus.total == doctest::Approx(aPlus.total).epsilon(0.05));
    }
    else
    {
        MESSAGE("WebGPU unavailable - terrain normal-map parity skipped");
    }

    if (vulkan != nullptr) { vulkan->Destroy(); }
    if (webgpu != nullptr) { webgpu->Destroy(); }
}

TEST_CASE("terrain probe: ARRAY normal + ORM blend through top-K, and the R2 rotation pin")
{
    // The per-LAYER (Texture2DArray) PBR path - which the base-normal probe does not touch, and
    // exactly where the WGSL cook can diverge (SampleGrad on the second/third array):
    //   1. a one-hot painted layer whose ARRAY normal tilts toward local +X shades directionally
    //      (aligned sun brighter than opposed);
    //   2. an ORM array with AO=0 kills the ambient term (darker than AO=1);
    //   3. R2: on a 90-degree-ROTATED terrain the perturbation FOLLOWS the chunk frame - the
    //      world-X sun pair becomes symmetric and the world-Z pair carries the asymmetry
    //      (a world-axis tangent frame would keep it on X and shear);
    //   4. WebGPU parity on the aligned case.
    auto run = [](rhi::Backend* backend, const Float3& toLight, u8 ao,
                  const Float4x4& chunkToWorld) -> Probe
    {
        rhi::Device* dev = (backend != nullptr) ? testsupport::MakeTestDevice(backend) : nullptr;
        if (dev == nullptr)
        {
            return Probe{};
        }
        Probe p;
        {
            engine::terrain::TerrainSplatTextureCache splatCache;
            engine::terrain::TerrainPaletteTextureCache paletteCache;
            RefPtr<tmodel::SplatWeights> sw = MakeStripeWeights(1); // one-hot layer 0 everywhere
            const Float3 gray{0.66f, 0.66f, 0.66f};
            RefPtr<tmodel::TerrainPaletteData> palette =
                MakePaletteData(Span<const Float3>{&gray, 1});
            // ARRAY normal: tangent-space (0.6, 0, 0.8) -> encoded (204, 128, 229); tilts the
            // shading normal toward the map's U axis (= local +X rotated by chunkToWorld).
            const usize sliceBytes = tmodel::TerrainPaletteData::SliceBytes(4, 1);
            palette->normalTexels.Resize(sliceBytes);
            palette->ormTexels.Resize(sliceBytes);
            for (usize t = 0; t < sliceBytes / 4; ++t)
            {
                u8* n = palette->normalTexels.Data() + t * 4;
                n[0] = 204; n[1] = 128; n[2] = 229; n[3] = 255;
                u8* o = palette->ormTexels.Data() + t * 4;
                o[0] = ao; o[1] = 255; o[2] = 0; o[3] = 255;
            }
            REQUIRE(palette->HasNormal());
            REQUIRE(palette->HasOrm());

            const f32 scale = 1000.0f;
            const engine::terrain::PaletteGpu gpu =
                paletteCache.GetOrCreate(*dev, *palette, Span<const f32>{&scale, 1});
            REQUIRE(gpu.arrayView != nullptr);
            REQUIRE(gpu.normalArrayView != nullptr);
            REQUIRE(gpu.ormArrayView != nullptr);
            const engine::terrain::SplatTextureViews views =
                splatCache.GetOrCreate(*dev, *sw, sw->Version());
            REQUIRE(views.weightView != nullptr);

            ProbeCfg cfg;
            cfg.terrain = MakeFlat();
            cfg.toLight = &toLight;
            cfg.chunkToWorld = chunkToWorld;
            cfg.weightView = views.weightView;
            cfg.indexView = views.indexView;
            cfg.paletteArrayView = gpu.arrayView;
            cfg.normalArrayView = gpu.normalArrayView;
            cfg.ormArrayView = gpu.ormArrayView;
            cfg.tileScaleBuffer = gpu.tileScaleBuffer;
            cfg.tileScaleGeneration = gpu.generation;
            cfg.paletteCount = 1;
            p = RenderTerrainProbe(*dev, cfg);

            splatCache.Clear(*dev);
            paletteCache.Clear(*dev);
        }
        dev->Destroy();
        return p;
    };

    const Float3 plusX = Normalized(Float3{0.85f, 0.5f, 0.0f});
    const Float3 minusX = Normalized(Float3{-0.85f, 0.5f, 0.0f});
    const Float3 plusZ = Normalized(Float3{0.0f, 0.5f, 0.85f});
    const Float3 minusZ = Normalized(Float3{0.0f, 0.5f, -0.85f});
    const Float4x4 identity = Float4x4::Identity();
    const Float4x4 rotated = Float4x4::RotationY(kPi * 0.5f);

    rhi::Backend* vulkan = nullptr;
    (void)rhi::vk::CreateBackend(rhi::vk::VkBackendDesc{}, vulkan);
    rhi::Backend* webgpu = nullptr;
    (void)rhi::webgpu::CreateBackend(rhi::webgpu::WebGpuBackendDesc{}, webgpu);

    const Probe nPlus = run(vulkan, plusX, 255, identity);
    if (!nPlus.valid)
    {
        MESSAGE("Vulkan unavailable - terrain array-PBR probe skipped");
        if (vulkan != nullptr) { vulkan->Destroy(); }
        if (webgpu != nullptr) { webgpu->Destroy(); }
        return;
    }
    const Probe nMinus = run(vulkan, minusX, 255, identity);
    const Probe aoDark = run(vulkan, plusX, 0, identity);
    std::printf("[terrain-arraypbr] +X=%.0f -X=%.0f ao0=%.0f\n", nPlus.total, nMinus.total,
                aoDark.total);
    // 1. The ARRAY normal makes flat ground directional (aligned sun brighter than opposed).
    CHECK(nPlus.total > nMinus.total * 1.10);
    // 2. AO=0 kills the ambient term (strictly darker with a real margin).
    CHECK(aoDark.total < nPlus.total * 0.95);

    // 3. R2 pin: rotate the terrain 90 degrees about Y - the perturbation follows the CHUNK frame,
    // so the X-sun pair evens out and the Z-sun pair carries the asymmetry.
    const Probe rotPX = run(vulkan, plusX, 255, rotated);
    const Probe rotMX = run(vulkan, minusX, 255, rotated);
    const Probe rotPZ = run(vulkan, plusZ, 255, rotated);
    const Probe rotMZ = run(vulkan, minusZ, 255, rotated);
    std::printf("[terrain-arraypbr] rot +X=%.0f -X=%.0f +Z=%.0f -Z=%.0f\n", rotPX.total,
                rotMX.total, rotPZ.total, rotMZ.total);
    CHECK(rotPX.total == doctest::Approx(rotMX.total).epsilon(0.05)); // asymmetry LEFT world X
    const f64 zAsym = Abs(rotPZ.total - rotMZ.total);
    CHECK(zAsym > 0.10 * rotPZ.total); // ... and moved to world Z (flip-agnostic magnitude)

    // 4. WebGPU parity on the aligned array-normal case (naga divergence surface).
    const Probe wPlus = run(webgpu, plusX, 255, identity);
    if (wPlus.valid)
    {
        CHECK(wPlus.total == doctest::Approx(nPlus.total).epsilon(0.05));
    }
    else
    {
        MESSAGE("WebGPU unavailable - array-PBR parity skipped");
    }

    if (vulkan != nullptr) { vulkan->Destroy(); }
    if (webgpu != nullptr) { webgpu->Destroy(); }
}
