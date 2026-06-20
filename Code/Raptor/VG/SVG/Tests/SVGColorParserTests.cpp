// Ported from Sedulous.VG.Tests/SVGColorParserTests.bf.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import raptor.core;
import raptor.vg.svg;

using namespace raptor::core;
using namespace raptor::vg::svg;

TEST_CASE("svg.color: hex6")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"#ff0000");
    REQUIRE(r.HasValue());
    CHECK(r.Value().r == 255);
    CHECK(r.Value().g == 0);
    CHECK(r.Value().b == 0);
}

TEST_CASE("svg.color: hex3")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"#f00");
    REQUIRE(r.HasValue());
    CHECK(r.Value().r == 255);
    CHECK(r.Value().g == 0);
    CHECK(r.Value().b == 0);
}

TEST_CASE("svg.color: named red")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"red");
    REQUIRE(r.HasValue());
    CHECK(r.Value().r == 255);
    CHECK(r.Value().g == 0);
    CHECK(r.Value().b == 0);
}

TEST_CASE("svg.color: named blue")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"blue");
    REQUIRE(r.HasValue());
    CHECK(r.Value().r == 0);
    CHECK(r.Value().g == 0);
    CHECK(r.Value().b == 255);
}

TEST_CASE("svg.color: rgb() function")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"rgb(128, 64, 32)");
    REQUIRE(r.HasValue());
    CHECK(r.Value().r == 128);
    CHECK(r.Value().g == 64);
    CHECK(r.Value().b == 32);
}

TEST_CASE("svg.color: hex mixed case")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"#FfAa00");
    REQUIRE(r.HasValue());
    CHECK(r.Value().r == 255);
    CHECK(r.Value().g == 170);
    CHECK(r.Value().b == 0);
}

TEST_CASE("svg.color: none is transparent")
{
    const Result<Color32> r = SVGColorParser::Parse(u8"none");
    REQUIRE(r.HasValue());
    CHECK(r.Value().a == 0);
}
