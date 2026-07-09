// Unit tests for the draconic.ui Core foundational types (Thickness, ViewTransform, ViewId).
// Ported alongside the types; will grow to mirror Sedulous.UI.Tests as the port progresses.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
namespace core = draconic::core;

TEST_CASE("ui.thickness: constructors + totals")
{
    CHECK(Thickness{}.IsZero());

    const Thickness all{ 4.0f };
    CHECK(all.Left == 4.0f);
    CHECK(all.Top == 4.0f);
    CHECK(all.Right == 4.0f);
    CHECK(all.Bottom == 4.0f);
    CHECK(all.TotalHorizontal() == 8.0f);
    CHECK(all.TotalVertical() == 8.0f);
    CHECK_FALSE(all.IsZero());

    const Thickness hv{ 3.0f, 5.0f };
    CHECK(hv.Left == 3.0f);
    CHECK(hv.Right == 3.0f);
    CHECK(hv.Top == 5.0f);
    CHECK(hv.Bottom == 5.0f);

    const Thickness each{ 1.0f, 2.0f, 3.0f, 4.0f };
    CHECK(each.TotalHorizontal() == 4.0f);   // 1 + 3
    CHECK(each.TotalVertical() == 6.0f);     // 2 + 4
}

TEST_CASE("ui.view-transform: identity + defaults")
{
    ViewTransform t;
    CHECK(t.IsIdentity());
    CHECK(t.Scale.x == 1.0f);
    CHECK(t.Scale.y == 1.0f);
    CHECK(t.Origin.x == 0.5f);
    CHECK(t.Origin.y == 0.5f);
    CHECK(ViewTransform::Identity.IsIdentity());

    t.Rotation = 0.5f;
    CHECK_FALSE(t.IsIdentity());

    ViewTransform s;
    s.Scale = core::Float2{ 2.0f, 2.0f };
    CHECK_FALSE(s.IsIdentity());
}

TEST_CASE("ui.view-id: unique, valid, invalid sentinel")
{
    CHECK_FALSE(ViewId::Invalid.IsValid());
    CHECK(ViewId::Invalid.RawValue() == 0u);

    const ViewId a = ViewId::Create();
    const ViewId b = ViewId::Create();
    CHECK(a.IsValid());
    CHECK(b.IsValid());
    CHECK_FALSE(a == b);              // unique
    CHECK(a == a);                   // reflexive
    CHECK(b.RawValue() == a.RawValue() + 1u); // monotonic
    CHECK_FALSE(a == ViewId::Invalid);
}
