// The mesh draw path through the new architecture: register a MeshRenderer with a
// RendererRegistry, drive a RenderFrame (Begin / AddView / End) over an ExtractedScene with
// a cube + camera, into a color target. Run on the Null RHI + real DXC: exercises the whole
// path (extraction snapshot, view draw-list sort, mesh upload, per-object UBO, PSO build,
// render pass, DrawIndexed) and verifies a pipeline was produced.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.rhi.null;
import raptor.geometry;
import raptor.materials;
import raptor.shaders;
import raptor.shaders.system;
import raptor.materials.pso;
import raptor.render;

using namespace raptor::core;
using namespace raptor::render;
namespace rhi = raptor::rhi;
namespace geo = raptor::geometry;
namespace mat = raptor::materials;
namespace shaders = raptor::shaders;

namespace {

// A small fixture holding the GPU-side systems + a color target + an encoder.
struct RenderHarness {
    shaders::Compiler* compiler = nullptr;
    rhi::null::NullDevice device;
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
    mat::PipelineStateCache psoCache(shaderSystem, h.device);

    MeshRenderer meshRenderer(h.device, shaderSystem, psoCache, /*framesInFlight*/ 2);
    REQUIRE(meshRenderer.Initialize().IsOk());
    RendererRegistry registry;
    registry.Register(&meshRenderer);
    RenderFrame frame(h.device, registry);

    // a scene snapshot: one cube at the origin
    RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(1.0f);
    RefPtr<mat::Material> material = mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build();
    ExtractedScene scene;
    MeshRenderData* rd = scene.Add<MeshRenderData>();
    rd->world = Mat4::Identity(); rd->mesh = cube.Get(); rd->material = material.Get();
    rd->category = RenderCategories::Opaque;

    ViewCamera camera;
    camera.view       = Mat4::LookAtRH(Vec3{ 0, 0, 5 }, Vec3{ 0, 0, 0 }, Vec3{ 0, 1, 0 });
    camera.projection = Mat4::PerspectiveFovRH(1.0472f, 1.0f, 0.1f, 100.0f);
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
    CHECK(psoCache.Size() == 1);
}

TEST_CASE("RenderFrame with an empty view still clears (no crash, no PSOs)")
{
    RenderHarness h;
    if (!h.Init(64, 64)) { return; }

    shaders::ShaderSystem shaderSystem(*h.compiler, h.device);
    mat::PipelineStateCache psoCache(shaderSystem, h.device);
    MeshRenderer meshRenderer(h.device, shaderSystem, psoCache, /*framesInFlight*/ 2);
    REQUIRE(meshRenderer.Initialize().IsOk());
    RendererRegistry registry;
    registry.Register(&meshRenderer);
    RenderFrame frame(h.device, registry);

    ExtractedScene scene;   // no renderables
    ViewCamera camera;
    ViewSettings settings;

    frame.Begin(*h.encoder, 0);
    frame.AddView(scene, camera, settings, h.colorView, rhi::TextureFormat::BGRA8Unorm, 64, 64);
    frame.End();
    CHECK(psoCache.Size() == 0);
}
