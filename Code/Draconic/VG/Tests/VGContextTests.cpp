// VGContext: batch production, transform/opacity, immediate-mode, images, commands.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.image;
import draconic.vg;

using namespace draconic::core;
using namespace draconic::vg;
namespace img = draconic::image;

TEST_CASE("vg.context: white texture sits at index 0")
{
    VGContext ctx;
    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.textures.Size() >= 1u);
    CHECK(batch.textures[0] != nullptr);
    CHECK(batch.textures[0]->Width() == 1u);
    CHECK(batch.textures[0]->Height() == 1u);
}

TEST_CASE("vg.context: FillRect produces geometry + a solid command")
{
    VGContext ctx;
    ctx.FillRect(Rectangle{ 0, 0, 10, 10 }, Color::Red);

    VGBatch& batch = ctx.GetBatch();
    CHECK(batch.VertexCount() > 0u);
    CHECK(batch.IndexCount() > 0u);
    REQUIRE(batch.CommandCount() == 1u);
    CHECK(batch.GetCommand(0).textureIndex == 0); // solid -> white texture
}

TEST_CASE("vg.context: transform is baked into emitted vertices")
{
    VGContext ctx;
    ctx.Translate(100.0f, 50.0f);
    ctx.FillRect(Rectangle{ 0, 0, 10, 10 }, Color::Green);

    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.VertexCount() > 0u);
    // Every vertex shifted by the translation (~100,50; allow <1px AA fringe slack).
    bool allShifted = true;
    for (usize i = 0; i < batch.VertexCount(); ++i)
        if (batch.vertices[i].position.x < 99.0f || batch.vertices[i].position.y < 49.0f) allShifted = false;
    CHECK(allShifted);
}

TEST_CASE("vg.context: opacity scales vertex alpha")
{
    VGContext ctx;
    ctx.PushOpacity(0.5f);
    ctx.FillRect(Rectangle{ 0, 0, 10, 10 }, ToColor(Color32{ 255, 255, 255, 255 }));
    ctx.PopOpacity();

    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.VertexCount() > 0u);
    // Inner (opaque) vertices should now carry ~half alpha.
    bool sawHalfAlpha = false;
    for (usize i = 0; i < batch.VertexCount(); ++i)
        if (batch.vertices[i].color.a > 120 && batch.vertices[i].color.a < 135) sawHalfAlpha = true;
    CHECK(sawHalfAlpha);
}

TEST_CASE("vg.context: state stack save/restore of transform")
{
    VGContext ctx;
    ctx.Translate(10.0f, 0.0f);
    ctx.PushState();
    ctx.Translate(90.0f, 0.0f);
    CHECK(ctx.GetTransform()(3, 0) == doctest::Approx(100.0f));
    ctx.PopState();
    CHECK(ctx.GetTransform()(3, 0) == doctest::Approx(10.0f));
}

TEST_CASE("vg.context: immediate-mode path fill")
{
    VGContext ctx;
    ctx.BeginPath();
    ctx.MoveTo(0, 0);
    ctx.LineTo(10, 0);
    ctx.LineTo(10, 10);
    ctx.ClosePath();
    ctx.Fill(Color::Blue, FillRule::NonZero, /*antiAlias*/ false);

    VGBatch& batch = ctx.GetBatch();
    CHECK(batch.VertexCount() == 3u);
    CHECK(batch.IndexCount() == 3u);
}

TEST_CASE("vg.context: DrawImage registers the texture and switches command")
{
    VGContext ctx;
    const u8 px[4] = { 10, 20, 30, 40 };
    img::OwnedImageData tex(1, 1, img::PixelFormat::RGBA8, Span<const u8>(px, 4));

    ctx.FillRect(Rectangle{ 0, 0, 5, 5 }, Color::Red); // solid command (tex 0)
    ctx.DrawImage(&tex, Vector2{ 0, 0 });              // textured command (tex 1)

    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.textures.Size() == 2u);
    CHECK(batch.textures[1] == &tex);
    REQUIRE(batch.CommandCount() == 2u);
    CHECK(batch.GetCommand(0).textureIndex == 0);
    CHECK(batch.GetCommand(1).textureIndex == 1);
}

TEST_CASE("vg.context: clear resets and re-seeds the white texture")
{
    VGContext ctx;
    ctx.FillRect(Rectangle{ 0, 0, 5, 5 }, Color::Red);
    ctx.Clear();
    VGBatch& batch = ctx.GetBatch();
    CHECK(batch.VertexCount() == 0u);
    CHECK(batch.CommandCount() == 0u);
    REQUIRE(batch.textures.Size() == 1u);
    CHECK(batch.textures[0]->Width() == 1u);
}
