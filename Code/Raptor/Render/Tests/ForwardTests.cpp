// Slice 2b — the forward renderer: initialize (registers the forward shader + layouts),
// then render an ExtractedView (a cube renderable + a camera) into a color target. Run
// on the Null RHI + real DXC: exercises the whole draw path (mesh upload, per-object UBO,
// PSO build, render pass, DrawIndexed) and verifies a pipeline was produced.
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

TEST_CASE("forward renderer: draws an extracted view (mesh upload + PSO + render pass)")
{
    shaders::Compiler* compiler = nullptr;
    if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk()) { MESSAGE("DXC unavailable; skipping"); return; }

    rhi::null::NullDevice device;
    shaders::ShaderSystem shaderSystem(*compiler, device);
    mat::PipelineStateCache psoCache(shaderSystem, device);

    ForwardRenderer renderer(device, shaderSystem, psoCache);
    REQUIRE(renderer.Initialize().IsOk());

    // an extracted view: one cube, viewed from (0,0,5)
    RefPtr<geo::StaticMesh> cube = geo::Primitives::Cube(1.0f);
    RefPtr<mat::Material> material = mat::MaterialBuilder(u8"lit").Shader(u8"forward").Build();

    ExtractedView ev;
    ev.hasCamera  = true;
    ev.view       = Mat4::LookAtRH(Vec3{ 0, 0, 5 }, Vec3{ 0, 0, 0 }, Vec3{ 0, 1, 0 });
    ev.projection = Mat4::PerspectiveFovRH(1.0472f, 1.0f, 0.1f, 100.0f);
    ev.renderables.PushBack(Renderable{ Mat4::Identity(), cube.Get(), material.Get(), 1 });

    // a color render target + a command encoder (Null records no-ops)
    rhi::Texture* color = nullptr;
    REQUIRE(device.CreateTexture(rhi::TextureDesc::RenderTarget(rhi::TextureFormat::BGRA8Unorm, 256, 256), color).IsOk());
    rhi::TextureView* colorView = nullptr;
    rhi::TextureViewDesc cvd{}; cvd.format = rhi::TextureFormat::BGRA8Unorm;
    REQUIRE(device.CreateTextureView(color, cvd, colorView).IsOk());

    rhi::CommandPool* pool = nullptr;
    REQUIRE(device.CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk());
    rhi::CommandEncoder* encoder = nullptr;
    REQUIRE(pool->CreateEncoder(encoder).IsOk());

    renderer.Render(ev, *encoder, colorView, rhi::TextureFormat::BGRA8Unorm, 256, 256, rhi::ClearColor::CornflowerBlue());

    CHECK(psoCache.Size() >= 1);   // a pipeline was built for the cube's material

    // a second frame reuses the cached PSO + mesh buffers (no growth)
    renderer.Render(ev, *encoder, colorView, rhi::TextureFormat::BGRA8Unorm, 256, 256, rhi::ClearColor::CornflowerBlue());
    CHECK(psoCache.Size() == 1);

    device.DestroyTextureView(colorView);
    device.DestroyTexture(color);
    device.DestroyCommandPool(pool);
    compiler->Destroy();
}

TEST_CASE("forward renderer: an empty view still clears (no crash, no PSOs)")
{
    shaders::Compiler* compiler = nullptr;
    if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk()) { return; }

    rhi::null::NullDevice device;
    shaders::ShaderSystem shaderSystem(*compiler, device);
    mat::PipelineStateCache psoCache(shaderSystem, device);
    ForwardRenderer renderer(device, shaderSystem, psoCache);
    REQUIRE(renderer.Initialize().IsOk());

    rhi::Texture* color = nullptr;
    REQUIRE(device.CreateTexture(rhi::TextureDesc::RenderTarget(rhi::TextureFormat::BGRA8Unorm, 64, 64), color).IsOk());
    rhi::TextureView* colorView = nullptr;
    rhi::TextureViewDesc cvd{}; cvd.format = rhi::TextureFormat::BGRA8Unorm;
    REQUIRE(device.CreateTextureView(color, cvd, colorView).IsOk());
    rhi::CommandPool* pool = nullptr; REQUIRE(device.CreateCommandPool(rhi::QueueType::Graphics, pool).IsOk());
    rhi::CommandEncoder* encoder = nullptr; REQUIRE(pool->CreateEncoder(encoder).IsOk());

    ExtractedView ev;   // no renderables
    renderer.Render(ev, *encoder, colorView, rhi::TextureFormat::BGRA8Unorm, 64, 64, rhi::ClearColor::Black());
    CHECK(psoCache.Size() == 0);

    device.DestroyTextureView(colorView);
    device.DestroyTexture(color);
    device.DestroyCommandPool(pool);
    compiler->Destroy();
}
