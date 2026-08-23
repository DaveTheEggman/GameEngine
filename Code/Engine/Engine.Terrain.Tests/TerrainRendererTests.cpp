// The terrain draw path end-to-end on the Null RHI + real DXC: build a heightfield -> chunk model +
// GPU height texture -> ONE TerrainRenderData in an ExtractedScene, then drive a RenderFrame (Begin /
// AddView / End) with the TerrainRenderer registered on Opaque. Exercises the whole Phase C path
// (extraction snapshot, view cull, our Resolve = quadtree cull + LOD + per-chunk DrawIndexed, PSO
// build) AND compiles terrain.vs.hlsl / terrain.ps.hlsl through the shader system - a green run proves
// the shaders build (Resolve emits nothing without a PSO) and the chunk draws come out.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.rhi;
import foundation.rhi.null;
import foundation.shaders;
import foundation.shaders.system;
import foundation.render;
import foundation.heightfield;
import foundation.terrain;
import engine.terrain;

using namespace foundation::core;
using namespace foundation::render;
namespace rhi = foundation::rhi;
namespace shaders = foundation::shaders;
namespace hf = foundation::heightfield;
namespace tmodel = foundation::terrain;

namespace
{
    shaders::FileShaderSourceProvider& EngineShaderProvider()
    {
        static shaders::FileShaderSourceProvider provider;
        static bool initialized = false;
        if (!initialized)
        {
            initialized = true;
            REQUIRE(provider.Initialize(u8"" BUILTIN_ENGINE_SHADER_DIR).IsOk());
        }
        return provider;
    }

    void WireEngineShaders(shaders::ShaderSystem& ss)
    {
        ss.SetSourceProvider(&EngineShaderProvider());
        const StringView includePaths[] = {EngineShaderProvider().RootDirectory()};
        ss.SetIncludePaths(Span<const StringView>{includePaths, 1});
    }

    // Null RHI harness: color target + encoder + a real DXC compiler (as PipelineTests does).
    struct RenderHarness
    {
        shaders::Compiler* compiler = nullptr;
        rhi::null::NullDevice device{DefaultAllocator()};
        rhi::Texture* color = nullptr;
        rhi::TextureView* colorView = nullptr;
        rhi::CommandPool* pool = nullptr;
        rhi::CommandEncoder* encoder = nullptr;

        bool Init(u32 w, u32 h)
        {
            if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk())
            {
                return false;
            }
            if (!device
                     .CreateTexture(
                         rhi::TextureDesc::RenderTarget(rhi::TextureFormat::BGRA8Unorm, w, h), color)
                     .IsOk())
            {
                return false;
            }
            rhi::TextureViewDesc cvd{};
            cvd.format = rhi::TextureFormat::BGRA8Unorm;
            if (!device.CreateTextureView(color, cvd, colorView).IsOk())
            {
                return false;
            }
            if (!device.CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk())
            {
                return false;
            }
            return pool->CreateEncoder(encoder).IsOk();
        }
        ~RenderHarness()
        {
            if (colorView)
            {
                device.DestroyTextureView(colorView);
            }
            if (color)
            {
                device.DestroyTexture(color);
            }
            if (pool)
            {
                device.DestroyCommandPool(pool);
            }
            if (compiler)
            {
                compiler->Destroy();
            }
        }
    };

    // A 129-grid (2x2 chunks) over 128x128 world, Y range [0,10], rising along +X.
    RefPtr<hf::Heightfield> MakeRampX()
    {
        RefPtr<hf::Heightfield> h =
            MakeRef<hf::Heightfield>(DefaultAllocator(), 129, Float2{128.0f, 128.0f}, 0.0f, 10.0f);
        for (i32 z = 0; z < 129; ++z)
        {
            for (i32 x = 0; x < 129; ++x)
            {
                h->SetSample(x, z, static_cast<hf::Height>(static_cast<f32>(x) / 128.0f * 65535.0f));
            }
        }
        return h;
    }

    // Populate one whole-terrain render item from a heightfield (what the manager's extract does).
    void FillTerrainRenderData(engine::terrain::TerrainRenderData& rd, const hf::Heightfield& h,
                               Span<const tmodel::TerrainChunk> chunks,
                               const tmodel::TerrainQuadtree& tree, rhi::TextureView* heightView,
                               u16 rendererId)
    {
        static const f32 thresholds[] = {1.0f, 0.25f, 0.08f, 0.03f, 0.012f, 0.005f, 0.002f};
        rd.category = RenderCategories::Opaque;
        rd.rendererId = rendererId;
        rd.chunks = chunks.Data();
        rd.quadtree = &tree;
        rd.chunkCount = static_cast<u32>(chunks.Size());
        rd.heightView = heightView;
        rd.chunkToWorld = Float4x4::Identity();
        rd.gridSize = h.Size();
        rd.worldSizeXZ = h.WorldSize();
        rd.minY = h.MinY();
        rd.maxY = h.MaxY();
        for (u32 i = 0; i < 7; ++i)
        {
            rd.thresholds[i] = thresholds[i];
        }
        rd.thresholdCount = 7;
        rd.worldCenter = Float3{0.0f, 5.0f, 0.0f};
        rd.worldRadius = 100.0f;
    }
}

TEST_CASE("terrain renderer: visible chunks draw (extract -> Resolve -> per-LOD DrawIndexed)")
{
    RenderHarness harness;
    if (!harness.Init(256, 256))
    {
        MESSAGE("DXC/Null unavailable; skipping");
        return;
    }

    shaders::ShaderSystem shaderSystem(*harness.compiler, harness.device);
    WireEngineShaders(shaderSystem);

    engine::terrain::TerrainRenderer renderer(harness.device, shaderSystem, /*framesInFlight*/ 2);
    REQUIRE(renderer.Initialize().IsOk());

    RendererRegistry registry;
    registry.Register(&renderer);
    RenderFrame frame(harness.device, registry, /*framesInFlight*/ 2);

    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<tmodel::TerrainChunk> chunks;
    tmodel::BuildChunks(*h, chunks);
    REQUIRE(chunks.Size() == 4u);
    tmodel::TerrainQuadtree tree;
    tree.Build(Span<const tmodel::TerrainChunk>{chunks.Data(), chunks.Size()},
               tmodel::ChunksPerSide(h->Size()));

    engine::terrain::TerrainHeightTextureCache heightCache;
    rhi::TextureView* heightView = heightCache.GetOrCreate(harness.device, *h, Guid{}, 1);
    REQUIRE(heightView != nullptr);

    ExtractedScene scene;
    engine::terrain::TerrainRenderData* rd = scene.Add<engine::terrain::TerrainRenderData>();
    REQUIRE(rd != nullptr);
    FillTerrainRenderData(*rd, *h, Span<const tmodel::TerrainChunk>{chunks.Data(), chunks.Size()},
                          tree, heightView, renderer.RendererId());

    // Camera looking straight down at the terrain: all four chunks are inside the frustum.
    ViewCamera camera;
    camera.view = Float4x4::LookAtRH(Float3{0.0f, 300.0f, 0.1f}, Float3{0.0f, 0.0f, 0.0f},
                                     Float3{0.0f, 0.0f, 1.0f});
    camera.projection = Float4x4::PerspectiveFovRH(1.2f, 1.0f, 1.0f, 2000.0f);
    ViewSettings settings;

    frame.Begin(*harness.encoder, 0);
    frame.AddView(scene, camera, settings, harness.colorView, rhi::TextureFormat::BGRA8Unorm, 256,
                  256);
    CHECK(frame.ViewCount() == 1);
    frame.End();

    // Draws only come out once the PSO (hence terrain.vs/ps.hlsl) built AND Resolve LOD'd each chunk.
    CHECK(renderer.MaxChunksDrawn() == 4u);
}

TEST_CASE("terrain renderer: nothing drawn when the terrain is off-screen")
{
    RenderHarness harness;
    if (!harness.Init(256, 256))
    {
        return;
    }
    shaders::ShaderSystem shaderSystem(*harness.compiler, harness.device);
    WireEngineShaders(shaderSystem);

    engine::terrain::TerrainRenderer renderer(harness.device, shaderSystem, /*framesInFlight*/ 2);
    REQUIRE(renderer.Initialize().IsOk());
    RendererRegistry registry;
    registry.Register(&renderer);
    RenderFrame frame(harness.device, registry, /*framesInFlight*/ 2);

    RefPtr<hf::Heightfield> h = MakeRampX();
    Array<tmodel::TerrainChunk> chunks;
    tmodel::BuildChunks(*h, chunks);
    tmodel::TerrainQuadtree tree;
    tree.Build(Span<const tmodel::TerrainChunk>{chunks.Data(), chunks.Size()},
               tmodel::ChunksPerSide(h->Size()));
    engine::terrain::TerrainHeightTextureCache heightCache;
    rhi::TextureView* heightView = heightCache.GetOrCreate(harness.device, *h, Guid{}, 1);
    REQUIRE(heightView != nullptr);

    ExtractedScene scene;
    engine::terrain::TerrainRenderData* rd = scene.Add<engine::terrain::TerrainRenderData>();
    FillTerrainRenderData(*rd, *h, Span<const tmodel::TerrainChunk>{chunks.Data(), chunks.Size()},
                          tree, heightView, renderer.RendererId());

    // Camera far away looking further away: the whole terrain is outside the frustum.
    ViewCamera camera;
    camera.view = Float4x4::LookAtRH(Float3{5000.0f, 100.0f, 0.0f}, Float3{6000.0f, 100.0f, 0.0f},
                                     Float3{0.0f, 1.0f, 0.0f});
    camera.projection = Float4x4::PerspectiveFovRH(1.2f, 1.0f, 1.0f, 2000.0f);
    ViewSettings settings;

    frame.Begin(*harness.encoder, 0);
    frame.AddView(scene, camera, settings, harness.colorView, rhi::TextureFormat::BGRA8Unorm, 256,
                  256);
    frame.End();

    CHECK(renderer.MaxChunksDrawn() == 0u);
}
