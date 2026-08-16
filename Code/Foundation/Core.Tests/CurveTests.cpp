// Curve (foundation.core:curve) - the scalar keyframe sampler: exact key hits, per-segment
// interpolation (Constant/Linear/Cubic), end clamping, key ordering, and the Hermite tangent math.

#include <doctest/doctest.h>

import foundation.core;

using namespace foundation::core;

namespace
{
    CurveKey Key(f32 t, f32 v, CurveInterpolation interp = CurveInterpolation::Linear, f32 tin = 0.0f,
                 f32 tout = 0.0f)
    {
        CurveKey k;
        k.time = t;
        k.value = v;
        k.interpolation = interp;
        k.tangentIn = tin;
        k.tangentOut = tout;
        return k;
    }
}

TEST_CASE("curve: empty + single key")
{
    Curve empty;
    CHECK(empty.IsEmpty());
    CHECK(empty.Evaluate(0.0f) == 0.0f);
    CHECK(empty.Duration() == 0.0f);

    Curve one;
    one.AddKey(Key(0.5f, 7.0f));
    CHECK(one.KeyCount() == 1u);
    CHECK(one.Evaluate(0.0f) == 7.0f);   // clamped before
    CHECK(one.Evaluate(0.5f) == 7.0f);   // at
    CHECK(one.Evaluate(10.0f) == 7.0f);  // clamped after
    CHECK(one.Duration() == 0.5f);
}

TEST_CASE("curve: linear interpolation + exact key hits + end clamp")
{
    Curve c;
    c.AddKey(Key(0.0f, 0.0f));
    c.AddKey(Key(1.0f, 10.0f));
    c.AddKey(Key(2.0f, 20.0f));

    // Exact key hits.
    CHECK(c.Evaluate(0.0f) == doctest::Approx(0.0f));
    CHECK(c.Evaluate(1.0f) == doctest::Approx(10.0f));
    CHECK(c.Evaluate(2.0f) == doctest::Approx(20.0f));
    // Between keys.
    CHECK(c.Evaluate(0.5f) == doctest::Approx(5.0f));
    CHECK(c.Evaluate(1.25f) == doctest::Approx(12.5f));
    // Clamp outside the range.
    CHECK(c.Evaluate(-1.0f) == doctest::Approx(0.0f));
    CHECK(c.Evaluate(5.0f) == doctest::Approx(20.0f));
    CHECK(c.Duration() == doctest::Approx(2.0f));
}

TEST_CASE("curve: constant (step) interpolation holds the left value")
{
    Curve c;
    c.AddKey(Key(0.0f, 1.0f, CurveInterpolation::Constant));
    c.AddKey(Key(1.0f, 9.0f, CurveInterpolation::Constant));
    // The left key's mode drives the segment: hold 1.0 across [0,1), jump at the key.
    CHECK(c.Evaluate(0.0f) == doctest::Approx(1.0f));
    CHECK(c.Evaluate(0.5f) == doctest::Approx(1.0f));
    CHECK(c.Evaluate(0.999f) == doctest::Approx(1.0f));
    CHECK(c.Evaluate(1.0f) == doctest::Approx(9.0f));
}

TEST_CASE("curve: cubic Hermite - endpoints exact, flat tangents ease (smoothstep)")
{
    // Zero tangents on both ends over [0,1] with values 0..1 reduce Hermite to smoothstep
    // (3t^2 - 2t^3): endpoints exact, midpoint 0.5, symmetric.
    Curve c;
    c.AddKey(Key(0.0f, 0.0f, CurveInterpolation::Cubic, 0.0f, 0.0f));
    c.AddKey(Key(1.0f, 1.0f, CurveInterpolation::Cubic, 0.0f, 0.0f));
    CHECK(c.Evaluate(0.0f) == doctest::Approx(0.0f));
    CHECK(c.Evaluate(1.0f) == doctest::Approx(1.0f));
    CHECK(c.Evaluate(0.5f) == doctest::Approx(0.5f));               // smoothstep(0.5) = 0.5
    CHECK(c.Evaluate(0.25f) == doctest::Approx(0.15625f));          // 3*.0625 - 2*.015625
    // Symmetry: f(t) + f(1-t) == 1 for smoothstep.
    CHECK(c.Evaluate(0.25f) + c.Evaluate(0.75f) == doctest::Approx(1.0f));
}

TEST_CASE("curve: cubic with matched linear tangents equals the straight line")
{
    // Tangent = slope (rise/run) on both ends of a value-0..10 over-time-0..2 segment => the cubic
    // collapses to the linear line through the endpoints.
    Curve c;
    const f32 slope = 5.0f; // (10-0)/(2-0)
    c.AddKey(Key(0.0f, 0.0f, CurveInterpolation::Cubic, slope, slope));
    c.AddKey(Key(2.0f, 10.0f, CurveInterpolation::Cubic, slope, slope));
    CHECK(c.Evaluate(0.5f) == doctest::Approx(2.5f));
    CHECK(c.Evaluate(1.0f) == doctest::Approx(5.0f));
    CHECK(c.Evaluate(1.5f) == doctest::Approx(7.5f));
}

TEST_CASE("curve: AddKey keeps keys sorted regardless of insertion order")
{
    Curve c;
    c.AddKey(Key(2.0f, 20.0f));
    c.AddKey(Key(0.0f, 0.0f));
    c.AddKey(Key(1.0f, 10.0f));
    REQUIRE(c.KeyCount() == 3u);
    CHECK(c.Keys()[0].time == doctest::Approx(0.0f));
    CHECK(c.Keys()[1].time == doctest::Approx(1.0f));
    CHECK(c.Keys()[2].time == doctest::Approx(2.0f));
    // And sampling still works after out-of-order inserts.
    CHECK(c.Evaluate(0.5f) == doctest::Approx(5.0f));
    CHECK(c.Evaluate(1.5f) == doctest::Approx(15.0f));
}

TEST_CASE("curve: coincident keys jump to the later value")
{
    Curve c;
    c.AddKey(Key(0.0f, 0.0f));
    c.AddKey(Key(1.0f, 5.0f));
    c.AddKey(Key(1.0f, 50.0f)); // same time - a hard step
    CHECK(c.Evaluate(0.5f) == doctest::Approx(2.5f));
    // At/just before the coincident pair, the segment is degenerate -> later value.
    CHECK(c.Evaluate(1.0f) == doctest::Approx(50.0f));
}
