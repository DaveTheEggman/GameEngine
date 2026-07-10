// Smoke test for the toolkit vector fields: construct each, set/get value, fire OnValueChanged.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;

using namespace draconic::ui;
using namespace draconic::ui::toolkit;
using namespace draconic::core;
namespace core = draconic::core;

TEST_CASE("toolkit-vectorfields: Vector2FieldRoundTrip")
{
    auto f = core::MakeRef<Vector2Field>(core::DefaultAllocator());
    CHECK(f->ChildCount() == 2u);
    f->SetValue(Float2{ 3.0f, 4.0f });
    CHECK(f->Value().x == doctest::Approx(3.0f));
    CHECK(f->Value().y == doctest::Approx(4.0f));
    f->SetRange(-10.0, 10.0);
    f->SetStep(0.5);
    CHECK(f->Step() == doctest::Approx(0.5));
}

TEST_CASE("toolkit-vectorfields: Vector3And4")
{
    auto f3 = core::MakeRef<Vector3Field>(core::DefaultAllocator());
    CHECK(f3->ChildCount() == 3u);
    f3->SetValue(Float3{ 1.0f, 2.0f, 3.0f });
    CHECK(f3->Value().z == doctest::Approx(3.0f));

    auto f4 = core::MakeRef<Vector4Field>(core::DefaultAllocator());
    CHECK(f4->ChildCount() == 4u);
    f4->SetValue(Float4{ 1.0f, 2.0f, 3.0f, 4.0f });
    CHECK(f4->Value().w == doctest::Approx(4.0f));
    f4->SetDecimalPlaces(2);
    CHECK(f4->DecimalPlaces() == 2);
}

TEST_CASE("toolkit-vectorfields: QuaternionEulerRoundTrip")
{
    auto q = core::MakeRef<QuaternionField>(core::DefaultAllocator());
    CHECK(q->ChildCount() == 3u);
    // Identity quaternion -> zero Euler.
    q->SetValue(Quaternion::Identity);
    const Quaternion v = q->Value();
    CHECK(v.w == doctest::Approx(1.0f));
    CHECK(v.x == doctest::Approx(0.0f));
}
