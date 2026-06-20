// Ported from Sedulous.VG.Tests/ShapeBuilderTests.bf.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import raptor.core;
import raptor.vg;

using namespace raptor::core;
using namespace raptor::vg;

namespace
{
    i32 CountCommand(const Path& path, PathCommand which)
    {
        i32 count = 0;
        const Span<const PathCommand> cmds = path.Commands();
        for (usize i = 0; i < cmds.Size(); ++i)
            if (cmds[i] == which) ++count;
        return count;
    }
}

TEST_CASE("shapebuilder: rounded rect uniform radius has 4 corner arcs")
{
    PathBuilder builder;
    ShapeBuilder::BuildRoundedRect(Rect{0, 0, 100, 100}, CornerRadii(10.0f), builder);
    CHECK(CountCommand(builder.ToPath(), PathCommand::CubicTo) == 4);
}

TEST_CASE("shapebuilder: rounded rect per-corner radii")
{
    PathBuilder builder;
    ShapeBuilder::BuildRoundedRect(Rect{0, 0, 100, 100}, CornerRadii(5, 10, 15, 20), builder);
    CHECK(CountCommand(builder.ToPath(), PathCommand::CubicTo) == 4);
}

TEST_CASE("shapebuilder: circle is 4 cubics")
{
    PathBuilder builder;
    ShapeBuilder::BuildCircle(Vec2{50, 50}, 25, builder);
    CHECK(CountCommand(builder.ToPath(), PathCommand::CubicTo) == 4);
}

TEST_CASE("shapebuilder: hexagon has 5 LineTo")
{
    PathBuilder builder;
    ShapeBuilder::BuildRegularPolygon(Vec2{50, 50}, 25, 6, builder);
    CHECK(CountCommand(builder.ToPath(), PathCommand::LineTo) == 5);
}

TEST_CASE("shapebuilder: star correct point count")
{
    PathBuilder builder;
    ShapeBuilder::BuildStar(Vec2{50, 50}, 30, 15, 5, builder);
    CHECK(CountCommand(builder.ToPath(), PathCommand::LineTo) == 9);
}

TEST_CASE("shapebuilder: ellipse is 4 cubics")
{
    PathBuilder builder;
    ShapeBuilder::BuildEllipse(Vec2{50, 50}, 30, 20, builder);
    CHECK(CountCommand(builder.ToPath(), PathCommand::CubicTo) == 4);
}
