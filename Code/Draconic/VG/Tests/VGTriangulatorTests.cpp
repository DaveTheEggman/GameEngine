// Ported from Sedulous.VG.Tests/TriangulatorTests.bf.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.vg;

using namespace draconic::core;
using namespace draconic::vg;

TEST_CASE("triangulator: convex polygon -> correct triangle count")
{
    Vec2 points[4] = { {0,0}, {10,0}, {10,10}, {0,10} };
    Array<u32> indices;
    Triangulator::Triangulate(Span<const Vec2>(points, 4), FillRule::EvenOdd, indices);
    CHECK(indices.Size() == 6u);
}

TEST_CASE("triangulator: concave produces valid mesh")
{
    Vec2 points[6] = { {0,0}, {10,0}, {10,5}, {5,5}, {5,10}, {0,10} };
    Array<u32> indices;
    Triangulator::Triangulate(Span<const Vec2>(points, 6), FillRule::EvenOdd, indices);
    CHECK(indices.Size() == 12u);
    for (usize i = 0; i < indices.Size(); ++i)
        CHECK(indices[i] < 6u);
}

TEST_CASE("triangulator: polygon area CCW positive")
{
    Vec2 points[4] = { {0,0}, {10,0}, {10,10}, {0,10} };
    CHECK(Triangulator::PolygonArea(Span<const Vec2>(points, 4)) > 0.0f);
}

TEST_CASE("triangulator: polygon area CW negative")
{
    Vec2 points[4] = { {0,0}, {0,10}, {10,10}, {10,0} };
    CHECK(Triangulator::PolygonArea(Span<const Vec2>(points, 4)) < 0.0f);
}

TEST_CASE("triangulator: polygon area correct value")
{
    Vec2 points[4] = { {0,0}, {10,0}, {10,10}, {0,10} };
    CHECK(Abs(Triangulator::PolygonArea(Span<const Vec2>(points, 4)) - 100.0f) < 0.01f);
}

TEST_CASE("triangulator: point in triangle inside")
{
    CHECK(Triangulator::PointInTriangle(Vec2{5,5}, Vec2{0,0}, Vec2{10,0}, Vec2{5,10}));
}

TEST_CASE("triangulator: point in triangle outside")
{
    CHECK_FALSE(Triangulator::PointInTriangle(Vec2{15,5}, Vec2{0,0}, Vec2{10,0}, Vec2{5,10}));
}

TEST_CASE("triangulator: pentagon -> correct triangles")
{
    Vec2 points[5];
    for (i32 i = 0; i < 5; ++i)
    {
        const f32 angle = kTwoPi * static_cast<f32>(i) / 5.0f - kHalfPi;
        points[static_cast<usize>(i)] = Vec2{ Cos(angle) * 10.0f, Sin(angle) * 10.0f };
    }
    Array<u32> indices;
    Triangulator::Triangulate(Span<const Vec2>(points, 5), FillRule::EvenOdd, indices);
    CHECK(indices.Size() == 9u);
}
