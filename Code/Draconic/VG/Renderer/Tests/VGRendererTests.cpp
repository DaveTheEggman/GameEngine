// Headless VGRenderer test via the Null RHI backend: initialize the pipeline,
// build a VGBatch with VGContext, Prepare it into the frame buffers, and check
// the returned slice. (Render needs a live RenderPassEncoder, exercised by the
// real backends; this covers init + the upload/slice path device-agnostically.)
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.rhi;
import draconic.rhi.null;
import draconic.vg;
import draconic.vg.renderer;

using namespace draconic::core;
using namespace draconic::vg;
using namespace draconic::vg::renderer;
namespace rhi = draconic::rhi;

namespace
{
    rhi::ShaderModule* MakeModule(rhi::Device& d)
    {
        const u8 dummy[4] = { 0, 0, 0, 0 };
        rhi::ShaderModuleDesc desc{};
        desc.code = Span<const u8>(dummy, 4);
        rhi::ShaderModule* m = nullptr;
        (void)d.CreateShaderModule(desc, m);
        return m;
    }
}

TEST_CASE("vg.renderer: render-vertex packs + decodes sRGB")
{
    // White stays white (1,1,1); opaque alpha passes through.
    const VGRenderVertex v(VGVertex::Solid(Vec2{ 2.0f, 3.0f }, Color::White));
    CHECK(v.position[0] == doctest::Approx(2.0f));
    CHECK(v.position[1] == doctest::Approx(3.0f));
    CHECK(v.color[0] == doctest::Approx(1.0f));
    CHECK(v.color[3] == doctest::Approx(1.0f));
    CHECK(v.coverage == doctest::Approx(1.0f));

    // A mid-grey sRGB byte (188) decodes to ~0.5 linear, not 0.737.
    const VGRenderVertex g(VGVertex::Solid(Vec2{}, ToColor(Color32{ 188, 188, 188, 255 })));
    CHECK(g.color[0] > 0.45f);
    CHECK(g.color[0] < 0.55f);
}

TEST_CASE("vg.renderer: initialize + prepare a batch (headless Null backend)")
{
    rhi::null::NullDevice device;
    rhi::ShaderModule* vs = MakeModule(device);
    rhi::ShaderModule* fs = MakeModule(device);
    REQUIRE(vs != nullptr);
    REQUIRE(fs != nullptr);

    VGRenderer renderer;
    REQUIRE(renderer.Initialize(device, *vs, *fs, rhi::TextureFormat::BGRA8UnormSrgb, /*frameCount*/ 2).IsOk());
    CHECK(renderer.IsInitialized());

    // Produce a batch with VGContext.
    VGContext ctx;
    ctx.FillRect(Rect{ 10, 10, 100, 50 }, Color::Red);
    ctx.FillCircle(Vec2{ 50, 50 }, 20, Color::Blue);
    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.VertexCount() > 0u);

    renderer.BeginFrame(0);
    const VGRenderSlice slice = renderer.Prepare(batch, /*frameIndex*/ 0, 800, 600);
    CHECK(slice.isValid);
    CHECK(slice.drawCommandCount == static_cast<i32>(batch.CommandCount()));
    CHECK(slice.vertexByteOffset == 0u);

    // A second batch in the same frame gets a non-overlapping vertex range.
    VGContext ctx2;
    ctx2.FillRect(Rect{ 0, 0, 10, 10 }, Color::Green);
    const VGRenderSlice slice2 = renderer.Prepare(ctx2.GetBatch(), 0, 800, 600);
    CHECK(slice2.isValid);
    CHECK(slice2.vertexByteOffset > 0u);

    renderer.Dispose();
    CHECK_FALSE(renderer.IsInitialized());

    device.DestroyShaderModule(vs);
    device.DestroyShaderModule(fs);
}

TEST_CASE("vg.renderer: empty batch yields an invalid slice")
{
    rhi::null::NullDevice device;
    rhi::ShaderModule* vs = MakeModule(device);
    rhi::ShaderModule* fs = MakeModule(device);

    VGRenderer renderer;
    REQUIRE(renderer.Initialize(device, *vs, *fs, rhi::TextureFormat::BGRA8UnormSrgb, 1).IsOk());

    VGContext ctx; // nothing drawn
    renderer.BeginFrame(0);
    const VGRenderSlice slice = renderer.Prepare(ctx.GetBatch(), 0, 100, 100);
    CHECK_FALSE(slice.isValid);

    renderer.Dispose();
    device.DestroyShaderModule(vs);
    device.DestroyShaderModule(fs);
}
