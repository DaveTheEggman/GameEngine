// Headless VGRenderer test via the Null RHI backend: initialize the pipeline,
// build a VGBatch with VGContext, Prepare it into the frame buffers, and check
// the returned slice. (Render needs a live RenderPassEncoder, exercised by the
// real backends; this covers init + the upload/slice path device-agnostically.)
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.rhi;
import draconic.rhi.null;
import draconic.image;
import draconic.vg;
import draconic.vg.renderer;

using namespace draconic::core;
using namespace draconic::vg;
using namespace draconic::vg::renderer;
namespace rhi = draconic::rhi;
namespace image = draconic::image;

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
    const VGRenderVertex v(VGVertex::Solid(Float2{ 2.0f, 3.0f }, Color::White));
    CHECK(v.position[0] == doctest::Approx(2.0f));
    CHECK(v.position[1] == doctest::Approx(3.0f));
    CHECK(v.color[0] == doctest::Approx(1.0f));
    CHECK(v.color[3] == doctest::Approx(1.0f));
    CHECK(v.coverage == doctest::Approx(1.0f));

    // A mid-grey sRGB byte (188) decodes to ~0.5 linear, not 0.737.
    const VGRenderVertex g(VGVertex::Solid(Float2{}, ToColor(Color32{ 188, 188, 188, 255 })));
    CHECK(g.color[0] > 0.45f);
    CHECK(g.color[0] < 0.55f);
}

TEST_CASE("vg.renderer: initialize + prepare a batch (headless Null backend)")
{
    rhi::null::NullDevice device{DefaultAllocator()};
    rhi::ShaderModule* vs = MakeModule(device);
    rhi::ShaderModule* fs = MakeModule(device);
    REQUIRE(vs != nullptr);
    REQUIRE(fs != nullptr);

    VGRenderer renderer;
    REQUIRE(renderer.Initialize(device, *vs, *fs, rhi::TextureFormat::BGRA8UnormSrgb, /*frameCount*/ 2).IsOk());
    CHECK(renderer.IsInitialized());

    // Produce a batch with VGContext.
    VGContext ctx;
    ctx.FillRect(Rectangle{ 10, 10, 100, 50 }, Color::Red);
    ctx.FillCircle(Float2{ 50, 50 }, 20, Color::Blue);
    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.VertexCount() > 0u);

    renderer.BeginFrame(0);
    const VGRenderSlice slice = renderer.Prepare(batch, /*frameIndex*/ 0, 800, 600);
    CHECK(slice.isValid);
    CHECK(slice.drawCommandCount == static_cast<i32>(batch.CommandCount()));
    CHECK(slice.vertexByteOffset == 0u);

    // A second batch in the same frame gets a non-overlapping vertex range.
    VGContext ctx2;
    ctx2.FillRect(Rectangle{ 0, 0, 10, 10 }, Color::Green);
    const VGRenderSlice slice2 = renderer.Prepare(ctx2.GetBatch(), 0, 800, 600);
    CHECK(slice2.isValid);
    CHECK(slice2.vertexByteOffset > 0u);

    renderer.Dispose();
    CHECK_FALSE(renderer.IsInitialized());

    device.DestroyShaderModule(vs);
    device.DestroyShaderModule(fs);
}

TEST_CASE("vg.renderer: external texture register / rebind / unregister")
{
    rhi::null::NullDevice device{DefaultAllocator()};
    rhi::ShaderModule* vs = MakeModule(device);
    rhi::ShaderModule* fs = MakeModule(device);
    REQUIRE(vs != nullptr);
    REQUIRE(fs != nullptr);

    VGRenderer renderer;
    REQUIRE(renderer.Initialize(device, *vs, *fs, rhi::TextureFormat::RGBA16Float, /*frameCount*/ 2).IsOk());

    // A caller-owned view standing in for a viewport's offscreen color target.
    rhi::TextureDesc td = rhi::TextureDesc::RenderTarget(rhi::TextureFormat::RGBA16Float, 64, 48);
    rhi::Texture* tex = nullptr;
    REQUIRE(device.CreateTexture(td, tex).IsOk());
    rhi::TextureView* view = nullptr;
    REQUIRE(device.CreateTextureView(tex, rhi::TextureViewDesc{}, view).IsOk());

    // The identity key: a pixel-less ImageDataRef (dimensions only).
    image::ImageDataRef key(64, 48);
    CHECK_FALSE(renderer.IsExternalTextureRegistered(&key));

    renderer.RegisterExternalTexture(&key, view);
    CHECK(renderer.IsExternalTextureRegistered(&key));

    // DrawImage(key) now Prepares without trying to upload CPU pixels (the ref
    // has none) - the pre-registered external view is used instead.
    VGContext ctx;
    ctx.DrawImage(&key, Rectangle{ 0, 0, 64, 48 });
    renderer.BeginFrame(0);
    const VGRenderSlice slice = renderer.Prepare(ctx.GetBatch(), 0, 800, 600);
    CHECK(slice.isValid);

    // Rebinding the same key to a new view keeps a single entry (still external).
    rhi::TextureView* view2 = nullptr;
    REQUIRE(device.CreateTextureView(tex, rhi::TextureViewDesc{}, view2).IsOk());
    renderer.RegisterExternalTexture(&key, view2);
    CHECK(renderer.IsExternalTextureRegistered(&key));

    // Unregister drops it (without destroying the caller-owned view/texture).
    renderer.UnregisterExternalTexture(&key);
    CHECK_FALSE(renderer.IsExternalTextureRegistered(&key));
    renderer.UnregisterExternalTexture(&key); // unknown key -> no-op, no crash

    // The views/texture are still ours to destroy.
    device.DestroyTextureView(view);
    device.DestroyTextureView(view2);
    device.DestroyTexture(tex);

    renderer.Dispose();
    device.DestroyShaderModule(vs);
    device.DestroyShaderModule(fs);
}

TEST_CASE("vg.renderer: Dispose leaves a still-registered external view intact")
{
    rhi::null::NullDevice device{DefaultAllocator()};
    rhi::ShaderModule* vs = MakeModule(device);
    rhi::ShaderModule* fs = MakeModule(device);

    rhi::TextureDesc td = rhi::TextureDesc::RenderTarget(rhi::TextureFormat::RGBA16Float, 32, 32);
    rhi::Texture* tex = nullptr;
    REQUIRE(device.CreateTexture(td, tex).IsOk());
    rhi::TextureView* view = nullptr;
    REQUIRE(device.CreateTextureView(tex, rhi::TextureViewDesc{}, view).IsOk());

    {
        VGRenderer renderer;
        REQUIRE(renderer.Initialize(device, *vs, *fs, rhi::TextureFormat::RGBA16Float, 1).IsOk());
        image::ImageDataRef key(32, 32);
        renderer.RegisterExternalTexture(&key, view);
        // Dispose() -> ClearTextureCache() must NOT destroy the external view.
    }

    // Still valid: we can destroy it ourselves (a double-free would trip ASAN).
    device.DestroyTextureView(view);
    device.DestroyTexture(tex);

    device.DestroyShaderModule(vs);
    device.DestroyShaderModule(fs);
}

TEST_CASE("vg.renderer: empty batch yields an invalid slice")
{
    rhi::null::NullDevice device{DefaultAllocator()};
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

TEST_CASE("vg.renderer: ComputeScissor clamps to content then offsets to the viewport")
{
    using draconic::vg::renderer::VGRenderer;

    // Fully inside: clamp is a no-op, the viewport origin offsets the rect.
    {
        const auto s = VGRenderer::ComputeScissor(Rectangle{ 10.0f, 20.0f, 100.0f, 50.0f },
                                                  400, 300, 400, 300);
        CHECK(s.x == 410);
        CHECK(s.y == 320);
        CHECK(s.width == 100u);
        CHECK(s.height == 50u);
    }
    // Overhanging the content box: clamped to (0..w, 0..h) BEFORE the offset - a clip
    // rect can never reach outside its view's rect (split-screen halves stay sealed).
    {
        const auto s = VGRenderer::ComputeScissor(Rectangle{ -30.0f, -10.0f, 500.0f, 400.0f },
                                                  400, 0, 400, 300);
        CHECK(s.x == 400);
        CHECK(s.y == 0);
        CHECK(s.width == 400u);
        CHECK(s.height == 300u);
    }
    // Entirely outside the content box: degenerates to zero size (nothing drawn).
    {
        const auto s = VGRenderer::ComputeScissor(Rectangle{ 500.0f, 0.0f, 50.0f, 50.0f },
                                                  0, 0, 400, 300);
        CHECK(s.width == 0u);
    }
    // Full-target viewport (the classic overload's path): identity behavior.
    {
        const auto s = VGRenderer::ComputeScissor(Rectangle{ 10.0f, 10.0f, 50.0f, 50.0f },
                                                  0, 0, 800, 600);
        CHECK(s.x == 10);
        CHECK(s.y == 10);
        CHECK(s.width == 50u);
        CHECK(s.height == 50u);
    }
}
