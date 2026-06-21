// Extra coverage (not from Sedulous.VG.Tests): exercise SVGLoader + SVGRenderer
// end-to-end on a small document.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import raptor.core;
import raptor.vg;
import raptor.vg.svg;

using namespace raptor::core;
using namespace raptor::vg;
using namespace raptor::vg::svg;

TEST_CASE("svg.loader: parses viewBox + shapes")
{
    const StringView svg =
        u8"<svg viewBox=\"0 0 100 100\">"
        u8"  <rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff0000\"/>"
        u8"  <circle cx=\"50\" cy=\"50\" r=\"20\" fill=\"blue\" stroke=\"black\" stroke-width=\"2\"/>"
        u8"</svg>";

    const Result<SVGDocument> r = SVGLoader::Load(svg);
    REQUIRE(r.HasValue());
    const SVGDocument& doc = r.Value();
    CHECK(doc.width == doctest::Approx(100.0f));
    CHECK(doc.height == doctest::Approx(100.0f));
    REQUIRE(doc.elements.Size() == 2u);

    CHECK(doc.elements[0].type == SVGElementType::Rect);
    REQUIRE(doc.elements[0].fillColor.HasValue());
    CHECK(doc.elements[0].fillColor.Value() == Color::Red);
    CHECK(doc.elements[0].path.HasValue());

    CHECK(doc.elements[1].type == SVGElementType::Circle);
    REQUIRE(doc.elements[1].strokeColor.HasValue());
    CHECK(doc.elements[1].strokeColor.Value() == Color::Black);
    CHECK(doc.elements[1].strokeWidth == doctest::Approx(2.0f));
}

TEST_CASE("svg.loader: nested group with transform")
{
    const StringView svg =
        u8"<svg width=\"64\" height=\"64\">"
        u8"  <g transform=\"translate(10,10)\">"
        u8"    <path d=\"M0 0 L10 0 L10 10 Z\" fill=\"green\"/>"
        u8"  </g>"
        u8"</svg>";

    const Result<SVGDocument> r = SVGLoader::Load(svg);
    REQUIRE(r.HasValue());
    const SVGDocument& doc = r.Value();
    REQUIRE(doc.elements.Size() == 1u);
    CHECK(doc.elements[0].type == SVGElementType::Group);
    CHECK(doc.elements[0].IsGroup());
    REQUIRE(doc.elements[0].children.Size() == 1u);
    CHECK(doc.elements[0].children[0].type == SVGElementType::Path);
    // translate(10,10) lands in the transform's last row.
    CHECK(doc.elements[0].transform.m[3][0] == doctest::Approx(10.0f));
}

TEST_CASE("svg.renderer: renders a document into a VG batch")
{
    const StringView svg =
        u8"<svg viewBox=\"0 0 100 100\">"
        u8"  <rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#00ff00\"/>"
        u8"</svg>";

    const Result<SVGDocument> r = SVGLoader::Load(svg);
    REQUIRE(r.HasValue());

    VGContext ctx;
    SVGRenderer::Render(ctx, r.Value(), Rect{ 0, 0, 200, 200 });

    VGBatch& batch = ctx.GetBatch();
    CHECK(batch.VertexCount() > 0u);
    CHECK(batch.IndexCount() > 0u);
}

TEST_CASE("svg.renderer: tint overrides fill")
{
    const StringView svg = u8"<svg viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\" fill=\"red\"/></svg>";
    const Result<SVGDocument> r = SVGLoader::Load(svg);
    REQUIRE(r.HasValue());

    VGContext ctx;
    SVGRenderer::Render(ctx, r.Value(), Rect{ 0, 0, 10, 10 }, Optional<Color>(Color::Blue));
    CHECK(ctx.GetBatch().VertexCount() > 0u);
}
