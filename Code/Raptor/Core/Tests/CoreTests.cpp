// Raptor::Core — smoke tests.
//
// Primary purpose at Phase 0: prove `import raptor.core;` compiles, links, and
// runs. Grows into real per-subsystem coverage as Core fills out.

#include <doctest/doctest.h>

import raptor.core;

using namespace raptor::core;

TEST_CASE("base: fundamental type widths")
{
    CHECK(sizeof(i8) == 1);
    CHECK(sizeof(i16) == 2);
    CHECK(sizeof(i32) == 4);
    CHECK(sizeof(i64) == 8);
    CHECK(sizeof(u8) == 1);
    CHECK(sizeof(u16) == 2);
    CHECK(sizeof(u32) == 4);
    CHECK(sizeof(u64) == 8);
    CHECK(sizeof(f32) == 4);
    CHECK(sizeof(f64) == 8);
    CHECK(sizeof(widechar) == 2); // UTF-16 wide unit
    CHECK(sizeof(utf8char) == 1);
}

TEST_CASE("base: Min / Max / Clamp")
{
    CHECK(Min(3, 5) == 3);
    CHECK(Max(3, 5) == 5);
    CHECK(Clamp(10, 0, 5) == 5);
    CHECK(Clamp(-2, 0, 5) == 0);
    CHECK(Clamp(3, 0, 5) == 3);

    // constexpr-usable
    static_assert(Min(1, 2) == 1);
    static_assert(Clamp(7, 0, 4) == 4);
}

TEST_CASE("base: ArrayCount")
{
    int values[4]{};
    CHECK(ArrayCount(values) == 4u);
    static_assert(ArrayCount(values) == 4u);
}
