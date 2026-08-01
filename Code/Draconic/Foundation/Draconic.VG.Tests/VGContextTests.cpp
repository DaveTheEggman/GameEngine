// VGContext: batch production, transform/opacity, immediate-mode, images, commands.
#include <doctest/doctest.h>
#include "Draconic.Core/Prelude.h"
import draconic.core;
import draconic.image;
import draconic.vg;

using namespace draconic::core;
using namespace draconic::vg;
namespace image = draconic::image;

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
    ctx.FillRect(Rectangle{0, 0, 10, 10}, Color::Red);

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
    ctx.FillRect(Rectangle{0, 0, 10, 10}, Color::Green);

    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.VertexCount() > 0u);
    // Every vertex shifted by the translation (~100,50; allow <1px AA fringe slack).
    bool allShifted = true;
    for (usize i = 0; i < batch.VertexCount(); ++i)
        if (batch.vertices[i].position.x < 99.0f || batch.vertices[i].position.y < 49.0f)
            allShifted = false;
    CHECK(allShifted);
}

TEST_CASE("vg.context: opacity scales vertex alpha")
{
    VGContext ctx;
    ctx.PushOpacity(0.5f);
    ctx.FillRect(Rectangle{0, 0, 10, 10}, ToColor(Color32{255, 255, 255, 255}));
    ctx.PopOpacity();

    VGBatch& batch = ctx.GetBatch();
    REQUIRE(batch.VertexCount() > 0u);
    // Inner (opaque) vertices should now carry ~half alpha.
    bool sawHalfAlpha = false;
    for (usize i = 0; i < batch.VertexCount(); ++i)
        if (batch.vertices[i].color.a > 0.47f && batch.vertices[i].color.a < 0.53f)
            sawHalfAlpha = true;
    CHECK(sawHalfAlpha);
}

TEST_CASE("vg.context: gradient fill bakes + binds a ramp LUT")
{
    VGContext ctx;
    VGLinearGradientFill grad(Float2{0.0f, 0.0f}, Float2{10.0f, 0.0f});
    grad.AddStop(0.0f, Color::Red);
    grad.AddStop(1.0f, Color::Blue);

    PathBuilder pb;
    pb.MoveTo(0, 0);
    pb.LineTo(10, 0);
    pb.LineTo(10, 10);
    pb.LineTo(0, 10);
    pb.Close();
    ctx.FillPath(pb.ToPath(), grad, FillRule::NonZero, /*antiAlias*/ false);

    VGBatch& batch = ctx.GetBatch();
    // A ramp LUT texture was registered beyond the index-0 white passthrough.
    CHECK(batch.textures.Size() >= 2u);
    // Gradient vertices carry white (the LUT supplies color) with non-solid texcoords, so the
    // ramp is sampled per pixel rather than Gouraud-interpolated.
    bool sawGradientVertex = false;
    for (usize i = 0; i < batch.VertexCount(); ++i)
        if (batch.vertices[i].color == Color::White &&
            batch.vertices[i].texCoord.x != VGVertex::SolidUV)
            sawGradientVertex = true;
    CHECK(sawGradientVertex);

    // Clearing frees the per-frame LUT pool and re-seats only the white texture.
    ctx.Clear();
    CHECK(ctx.GetBatch().textures.Size() == 1u);
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
    const u8 px[4] = {10, 20, 30, 40};
    image::OwnedImageData tex(1, 1, image::PixelFormat::RGBA8, Span<const u8>(px, 4));

    ctx.FillRect(Rectangle{0, 0, 5, 5}, Color::Red); // solid command (tex 0)
    ctx.DrawImage(&tex, Float2{0, 0});               // textured command (tex 1)

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
    ctx.FillRect(Rectangle{0, 0, 5, 5}, Color::Red);
    ctx.Clear();
    VGBatch& batch = ctx.GetBatch();
    CHECK(batch.VertexCount() == 0u);
    CHECK(batch.CommandCount() == 0u);
    REQUIRE(batch.textures.Size() == 1u);
    CHECK(batch.textures[0]->Width() == 1u);
}
