// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// GUI - gradient + image/nine-slice drawable tests. Metadata + GPU-free geometry
// checks (VGContext tessellates into a CPU vertex batch).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.vg;
import foundation.image;
import experimental.gui;

using namespace experimental::gui;
namespace core = foundation::core;
namespace vg = foundation::vg;
namespace image = foundation::image;

namespace
{
    // A tiny 2x2 RGBA image for image-drawable tests (owns its pixels).
    image::OwnedImageData MakeImage()
    {
        static const core::u8 pixels[16] = {
            255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        };
        return image::OwnedImageData(2, 2, image::PixelFormat::RGBA8,
                                     core::Span<const core::u8>(pixels, 16));
    }
}

// === LinearGradientDrawable ===

TEST_CASE("gradient: linear stops and geometry")
{
    LinearGradientDrawable g;
    CHECK(g.StopCount() == 0);
    g.AddStop(0.0f, core::Color::Red);
    g.AddStop(1.0f, core::Color::Blue);
    CHECK(g.StopCount() == 2);

    vg::VGContext ctx;
    DrawContext dc{ctx};
    g.Draw(dc, Rect{0.0f, 0.0f, 100.0f, 40.0f});
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}

TEST_CASE("gradient: linear with no stops draws nothing")
{
    LinearGradientDrawable g;
    vg::VGContext ctx;
    DrawContext dc{ctx};
    g.Draw(dc, Rect{0.0f, 0.0f, 100.0f, 40.0f});
    CHECK(ctx.GetBatch().vertices.Size() == 0);
}

// === RadialGradientDrawable ===

TEST_CASE("gradient: radial defaults and geometry")
{
    RadialGradientDrawable g;
    CHECK(g.Center.x == doctest::Approx(0.5f));
    CHECK(g.Center.y == doctest::Approx(0.5f));
    CHECK(g.RadiusScale == doctest::Approx(1.0f));
    g.AddStop(0.0f, core::Color::White);
    g.AddStop(1.0f, core::Color::Red);

    vg::VGContext ctx;
    DrawContext dc{ctx};
    g.Draw(dc, Rect{0.0f, 0.0f, 80.0f, 80.0f});
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}

// === ImageDrawable ===

TEST_CASE("image: null image has no intrinsic size and draws nothing")
{
    ImageDrawable d;
    CHECK_FALSE(d.IntrinsicSize().HasValue());

    vg::VGContext ctx;
    DrawContext dc{ctx};
    d.Draw(dc, Rect{0.0f, 0.0f, 10.0f, 10.0f});
    CHECK(ctx.GetBatch().vertices.Size() == 0);
}

TEST_CASE("image: intrinsic size and geometry")
{
    image::OwnedImageData img = MakeImage();
    ImageDrawable d{&img};
    REQUIRE(d.IntrinsicSize().HasValue());
    CHECK(d.IntrinsicSize().Value().x == doctest::Approx(2.0f));
    CHECK(d.IntrinsicSize().Value().y == doctest::Approx(2.0f));

    vg::VGContext ctx;
    DrawContext dc{ctx};
    d.Draw(dc, Rect{0.0f, 0.0f, 32.0f, 32.0f});
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}

// === NineSliceDrawable ===

TEST_CASE("nine-slice: intrinsic size and geometry")
{
    image::OwnedImageData img = MakeImage();
    NineSliceDrawable d{&img, image::NineSlice{1.0f, 1.0f, 1.0f, 1.0f}};
    REQUIRE(d.IntrinsicSize().HasValue());
    CHECK(d.IntrinsicSize().Value().x == doctest::Approx(2.0f));

    vg::VGContext ctx;
    DrawContext dc{ctx};
    d.Draw(dc, Rect{0.0f, 0.0f, 48.0f, 48.0f});
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}
