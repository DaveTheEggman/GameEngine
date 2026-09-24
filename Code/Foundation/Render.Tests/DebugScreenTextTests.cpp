// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Debug-draw 2D text placement: the right-aligned encoding DrawScreenTextRight stores and
// the pixel x the screen pass resolves it to.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.render;

using namespace foundation::core;
namespace debug = foundation::render::debug;

TEST_CASE("debug-draw: DrawScreenTextRight stores the margin as a negative x and the pass "
          "resolves it against the viewport width")
{
    debug::DebugDraw dd;
    dd.DrawScreenText(12.0f, 12.0f, u8"status", Color{1, 1, 1, 1});
    dd.DrawScreenTextRight(12.0f, 12.0f, u8"60 fps  16.7 ms", Color{1, 1, 1, 1});
    REQUIRE(dd.Commands2D().Size() == 2u);
    const debug::Debug2DCommand& left = dd.Commands2D()[0];
    const debug::Debug2DCommand& right = dd.Commands2D()[1];
    CHECK(left.position.x == 12.0f);
    CHECK(right.position.x < 0.0f); // the encoding: -(margin + 1)
    CHECK(right.textLength == 15);

    const f32 glyph = static_cast<f32>(debug::kCharWidth);
    // A left x passes through; a right x lands so the text ENDS 12 px in from the right edge.
    CHECK(debug::ResolveScreenTextX(left.position.x, left.textLength, glyph, 800) == 12.0f);
    const f32 x = debug::ResolveScreenTextX(right.position.x, right.textLength, glyph, 800);
    CHECK(x == doctest::Approx(800.0f - 12.0f - 15.0f * glyph));
    CHECK(x + 15.0f * glyph == doctest::Approx(788.0f));
    // Scale widens the glyph cell and the resolve follows it.
    const f32 x2 = debug::ResolveScreenTextX(right.position.x, right.textLength, glyph * 2.0f, 800);
    CHECK(x2 == doctest::Approx(800.0f - 12.0f - 15.0f * glyph * 2.0f));
}
