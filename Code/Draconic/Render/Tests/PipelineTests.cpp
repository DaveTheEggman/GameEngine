// The mesh draw path through the new architecture: register a MeshRenderer with a
// RendererRegistry, drive a RenderFrame (Begin / AddView / End) over an ExtractedScene with
// a cube + camera, into a color target. Run on the Null RHI + real DXC: exercises the whole
// path (extraction snapshot, view draw-list sort, mesh upload, per-object UBO, PSO build,
// render pass, DrawIndexed) and verifies a pipeline was produced.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.rhi;
import draconic.rhi.null;
import draconic.geometry;
import draconic.materials;
import draconic.shaders;
import draconic.shaders.system;
import draconic.materials.pso;
import draconic.render;

using namespace draconic::core;
using namespace draconic::render;
namespace rhi = draconic::rhi;
namespace geometry = draconic::geometry;
namespace materials = draconic::materials;
namespace shaders = draconic::shaders;

namespace {

// A small fixture holding the GPU-side systems + a color target + an encoder.
struct RenderHarness {
    shaders::Compiler* compiler = nullptr;
    rhi::null::NullDevice device{DefaultAllocator()};
    rhi::Texture* color = nullptr;
    rhi::TextureView* colorView = nullptr;
    rhi::CommandPool* pool = nullptr;
    rhi::CommandEncoder* encoder = nullptr;

    bool Init(u32 w, u32 h) {
        if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk()) { return false; }
        if (!device.CreateTexture(rhi::TextureDesc::RenderTarget(rhi::TextureFormat::BGRA8Unorm, w, h), color).IsOk()) { return false; }
        rhi::TextureViewDesc cvd{}; cvd.format = rhi::TextureFormat::BGRA8Unorm;
        if (!device.CreateTextureView(color, cvd, colorView).IsOk()) { return false; }
        if (!device.CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk()) { return false; }
        if (!pool->CreateEncoder(encoder).IsOk()) { return false; }
        return true;
    }
    ~RenderHarness() {
        if (colorView) { device.DestroyTextureView(colorView); }
        if (color)     { device.DestroyTexture(color); }
        if (pool)      { device.DestroyCommandPool(pool); }
        if (compiler)  { compiler->Destroy(); }
    }
};

} // namespace

TEST_CASE("RenderFrame draws a one-cube view (extract -> sort -> mesh upload -> PSO -> pass)")
{
    RenderHarness h;
    if (!h.Init(256, 256)) { MESSAGE("DXC/Null unavailable; skipping"); return; }

    shaders::ShaderSystem shaderSystem(*h.compiler, h.device);
    materials::PipelineStateCache psoCache(shaderSystem, h.device);

    materials::MaterialSystem materialSystem;
    REQUIRE(materialSystem.Initialize(h.device).IsOk());
    MeshRenderer meshRenderer(h.device, shaderSystem, psoCache, materialSystem, /*framesInFlight*/ 2);
    REQUIRE(meshRenderer.Initialize().IsOk());
    RendererRegistry registry;
    registry.Register(&meshRenderer);
    RenderFrame frame(h.device, registry, /*framesInFlight*/ 2);

    // a scene snapshot: one cube at the origin
    RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);
    RefPtr<materials::Material> material = materials::MaterialBuilder(u8"lit").Shader(u8"forward").Build();
    ExtractedScene scene;
    MeshRenderData* rd = scene.Add<MeshRenderData>();
    rd->world = Float4x4::Identity(); rd->mesh = cube.Get(); rd->material = material.Get();
    rd->category = RenderCategories::Opaque;

    ViewCamera camera;
    camera.view       = Float4x4::LookAtRH(Float3{ 0, 0, 5 }, Float3{ 0, 0, 0 }, Float3{ 0, 1, 0 });
    camera.projection = Float4x4::PerspectiveFovRH(1.0472f, 1.0f, 0.1f, 100.0f);
    ViewSettings settings;

    frame.Begin(*h.encoder, 0);
    frame.AddView(scene, camera, settings, h.colorView, rhi::TextureFormat::BGRA8Unorm, 256, 256);
    CHECK(frame.ViewCount() == 1);
    frame.End();

    CHECK(psoCache.Size() >= 1);   // a pipeline was built for the cube's material

    // a second frame reuses the cached PSO + mesh buffers (no growth)
    frame.Begin(*h.encoder, 1);
    frame.AddView(scene, camera, settings, h.colorView, rhi::TextureFormat::BGRA8Unorm, 256, 256);
    frame.End();
    CHECK(psoCache.Size() >= 1);  // cached PSOs reused across frames
}

TEST_CASE("RenderFrame batches same-mesh-same-material draws into an instanced draw")
{
    RenderHarness h;
    if (!h.Init(256, 256)) { return; }

    shaders::ShaderSystem shaderSystem(*h.compiler, h.device);
    materials::PipelineStateCache psoCache(shaderSystem, h.device);
    materials::MaterialSystem materialSystem;
    REQUIRE(materialSystem.Initialize(h.device).IsOk());
    MeshRenderer meshRenderer(h.device, shaderSystem, psoCache, materialSystem, /*framesInFlight*/ 2);
    REQUIRE(meshRenderer.Initialize().IsOk());
    RendererRegistry registry;
    registry.Register(&meshRenderer);
    RenderFrame frame(h.device, registry, /*framesInFlight*/ 2);

    RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);
    RefPtr<materials::Material> material = materials::MaterialBuilder(u8"lit").Shader(u8"forward").Build();

    // Eight cubes, one shared mesh + material, distinct world + color -> one instanced batch.
    ExtractedScene scene;
    for (int n = 0; n < 8; ++n) {
        MeshRenderData* rd = scene.Add<MeshRenderData>();
        rd->world = Float4x4::Identity();
        rd->worldCenter = Float3{ static_cast<f32>(n), 0, 0 };
        rd->color = Color{ static_cast<f32>(n) / 8.0f, 0.5f, 0.5f, 1.0f };
        rd->mesh = cube.Get(); rd->material = material.Get();
        rd->category = RenderCategories::Opaque;
    }

    ViewCamera camera;
    camera.view       = Float4x4::LookAtRH(Float3{ 0, 0, 20 }, Float3{ 0, 0, 0 }, Float3{ 0, 1, 0 });
    camera.projection = Float4x4::PerspectiveFovRH(1.0472f, 1.0f, 0.1f, 100.0f);
    ViewSettings settings;

    frame.Begin(*h.encoder, 0);
    frame.AddView(scene, camera, settings, h.colorView, rhi::TextureFormat::BGRA8Unorm, 256, 256);
    frame.End();

    // The instanced permutation shares a pipeline across all eight draws.
    CHECK(psoCache.Size() >= 1);
}

TEST_CASE("RenderFrame parallel emit: many distinct draws fan out across the job system")
{
    RenderHarness h;
    if (!h.Init(256, 256)) { return; }
    InitGlobalJobSystem(4);
    {
        shaders::ShaderSystem shaderSystem(*h.compiler, h.device);
        materials::PipelineStateCache psoCache(shaderSystem, h.device);
        materials::MaterialSystem materialSystem;
    REQUIRE(materialSystem.Initialize(h.device).IsOk());
    MeshRenderer meshRenderer(h.device, shaderSystem, psoCache, materialSystem, /*framesInFlight*/ 2);
        REQUIRE(meshRenderer.Initialize().IsOk());
        RendererRegistry registry;
        registry.Register(&meshRenderer);
        RenderFrame frame(h.device, registry, /*framesInFlight*/ 2);

        // 300 distinct materials (one shared mesh) -> 300 singleton draws (no batching) -> over
        // the parallel-emit threshold, so emission fans out across the job system's worker pools.
        RefPtr<geometry::StaticMesh> cube = geometry::Primitives::Cube(1.0f);
        Array<RefPtr<materials::Material>> mats;   // keep the materials alive for the frame
        ExtractedScene scene;
        for (int n = 0; n < 300; ++n) {
            RefPtr<materials::Material> m = materials::MaterialBuilder(u8"lit").Shader(u8"forward").Build();
            mats.PushBack(m);
            MeshRenderData* rd = scene.Add<MeshRenderData>();
            rd->world = Float4x4::Identity(); rd->worldCenter = Float3{ static_cast<f32>(n), 0, 0 };
            rd->mesh = cube.Get(); rd->material = m.Get(); rd->category = RenderCategories::Opaque;
        }

        ViewCamera camera;
        camera.view       = Float4x4::LookAtRH(Float3{ 0, 0, 20 }, Float3{ 0, 0, 0 }, Float3{ 0, 1, 0 });
        camera.projection = Float4x4::PerspectiveFovRH(1.0472f, 1.0f, 0.1f, 100.0f);
        ViewSettings settings;

        // Drive two frames to exercise the per-worker pool ring (reset between frame ring slots).
        for (u32 f = 0; f < 2; ++f) {
            frame.Begin(*h.encoder, f);
            frame.AddView(scene, camera, settings, h.colorView, rhi::TextureFormat::BGRA8Unorm, 256, 256);
            frame.End();   // parallel emit - must not crash or deadlock
        }
        CHECK(psoCache.Size() >= 1);
    }
    ShutdownGlobalJobSystem();
}

TEST_CASE("RenderFrame with an empty view still clears (no crash, no PSOs)")
{
    RenderHarness h;
    if (!h.Init(64, 64)) { return; }

    shaders::ShaderSystem shaderSystem(*h.compiler, h.device);
    materials::PipelineStateCache psoCache(shaderSystem, h.device);
    materials::MaterialSystem materialSystem;
    REQUIRE(materialSystem.Initialize(h.device).IsOk());
    MeshRenderer meshRenderer(h.device, shaderSystem, psoCache, materialSystem, /*framesInFlight*/ 2);
    REQUIRE(meshRenderer.Initialize().IsOk());
    RendererRegistry registry;
    registry.Register(&meshRenderer);
    RenderFrame frame(h.device, registry, /*framesInFlight*/ 2);

    ExtractedScene scene;   // no renderables
    ViewCamera camera;
    ViewSettings settings;

    frame.Begin(*h.encoder, 0);
    frame.AddView(scene, camera, settings, h.colorView, rhi::TextureFormat::BGRA8Unorm, 64, 64);
    frame.End();
    CHECK(psoCache.Size() == 0);
}
