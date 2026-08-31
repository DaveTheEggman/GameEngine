// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// SplineComponent: serialization round-trips the authored point set (positions, handles,
// modes, closed flag) and rebuilds the derived arc-length cache on load.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.spline;
import engine.spline;

using namespace foundation::core;
using namespace foundation::spline;
using engine::spline::SplineComponent;

TEST_CASE("spline component: serialization round-trips points and rebuilds caches")
{
    SplineComponent authored;
    authored.curve.points.PushBack(SplinePoint{Float3{0, 0, 0}});
    authored.curve.points.PushBack(
        SplinePoint{Float3{5, 1, 0}, Float3{-1, 0, 0}, Float3{1, 0, 0},
                    SplineHandleMode::Broken});
    authored.curve.points.PushBack(SplinePoint{Float3{10, 0, 4}});
    authored.curve.closed = true;
    authored.curve.UpdateAutoHandles();
    authored.curve.RebuildArcLength();

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        Serialize(writer, authored);
        REQUIRE(writer.IsOk());
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    SplineComponent loaded;
    {
        BinarySerializer reader(buffer, SerializeMode::Read);
        Serialize(reader, loaded);
        REQUIRE(reader.IsOk());
    }

    REQUIRE(loaded.curve.points.Size() == 3u);
    CHECK(loaded.curve.closed);
    CHECK(Length(loaded.curve.points[1].position - Float3{5, 1, 0}) < 0.0001f);
    CHECK(Length(loaded.curve.points[1].inHandle - Float3{-1, 0, 0}) < 0.0001f);
    CHECK(loaded.curve.points[1].mode == SplineHandleMode::Broken);
    // Derived caches rebuilt on read: the loaded curve evaluates identically.
    CHECK(loaded.curve.Length() == doctest::Approx(authored.curve.Length()).epsilon(0.001));
    CHECK(Length(loaded.curve.Evaluate(1.5f) - authored.curve.Evaluate(1.5f)) < 0.0001f);
}
