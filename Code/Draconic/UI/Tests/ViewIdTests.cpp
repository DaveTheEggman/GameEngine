// Ported from Sedulous.UI.Tests/src/ViewIdTests.bf (faithful).
// NOTE: ToString_ContainsValue is NOT ported - Beef's ToString(String buffer) debug convention is not
// a Draconic idiom (formatting is via core::Format, and String has no Contains). Flagged divergence;
// the other 6 ViewId tests port 1:1.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;

TEST_CASE("view-id: Create_ReturnsValidId")
{
    ViewId id = ViewId::Create();
    CHECK(id.IsValid());
    CHECK(id.RawValue() > 0u);
}

TEST_CASE("view-id: Create_ReturnsUniqueIds")
{
    ViewId a = ViewId::Create();
    ViewId b = ViewId::Create();
    CHECK(a != b);
}

TEST_CASE("view-id: Invalid_IsNotValid")
{
    ViewId id = ViewId::Invalid;
    CHECK_FALSE(id.IsValid());
    CHECK(id.RawValue() == 0u);
}

TEST_CASE("view-id: Equality_SameValue")
{
    ViewId a = ViewId::Create();
    ViewId b = a;
    CHECK(a == b);
    CHECK(a.Equals(b));
}

TEST_CASE("view-id: Inequality_DifferentValues")
{
    ViewId a = ViewId::Create();
    ViewId b = ViewId::Create();
    CHECK(a != b);
    CHECK_FALSE(a.Equals(b));
}

TEST_CASE("view-id: GetHashCode_SameForEqual")
{
    ViewId a = ViewId::Create();
    ViewId b = a;
    CHECK(a.GetHashCode() == b.GetHashCode());
}
