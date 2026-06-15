// Raptor::Core — smoke tests.
//
// Primary purpose at Phase 0: prove `import raptor.core;` compiles, links, and
// runs. Grows into real per-subsystem coverage as Core fills out.

#include <doctest/doctest.h>

#include "Core/Debug/Assert.h"

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

TEST_CASE("base: Swap")
{
    int a = 1;
    int b = 2;
    Swap(a, b);
    CHECK(a == 2);
    CHECK(b == 1);
}

TEST_CASE("base: Status")
{
    Status ok;
    CHECK(ok.IsOk());
    CHECK(static_cast<bool>(ok));
    CHECK(ok.Code() == ErrorCode::Ok);

    Status bad = ErrorCode::NotFound;
    CHECK_FALSE(bad.IsOk());
    CHECK_FALSE(static_cast<bool>(bad));
    CHECK(bad.Code() == ErrorCode::NotFound);

    CHECK(ok == Status{});
    CHECK(bad == Status{ ErrorCode::NotFound });
}

TEST_CASE("base: Result value case")
{
    Result<int> r = 42;
    REQUIRE(r.HasValue());
    CHECK(static_cast<bool>(r));
    CHECK(r.Value() == 42);
    CHECK(r.ValueOr(-1) == 42);
}

TEST_CASE("base: Result error case")
{
    Result<int> r = Err(ErrorCode::OutOfRange);
    CHECK_FALSE(r.HasValue());
    CHECK_FALSE(static_cast<bool>(r));
    CHECK(r.Error() == ErrorCode::OutOfRange);
    CHECK(r.ValueOr(-1) == -1);
}

TEST_CASE("base: Result manages a non-trivial payload")
{
    struct Counter
    {
        static int& Live() { static int n = 0; return n; }
        Counter() { ++Live(); }
        Counter(const Counter&) { ++Live(); }
        Counter(Counter&&) { ++Live(); }
        ~Counter() { --Live(); }
    };

    CHECK(Counter::Live() == 0);
    {
        Result<Counter> r{ Counter{} };
        CHECK(r.HasValue());
        CHECK(Counter::Live() == 1);

        Result<Counter> e = Err(ErrorCode::Internal);
        CHECK_FALSE(e.HasValue());
        CHECK(Counter::Live() == 1); // error case constructs no Counter
    }
    CHECK(Counter::Live() == 0); // all destroyed
}

// --- Debug / assertions ----------------------------------------------------
// We install a non-breaking handler so failed asserts record instead of trap.

namespace
{
    int g_assertCount = 0;

    bool RecordingHandler(const char*, const char*, const char*, int, const char*) noexcept
    {
        ++g_assertCount;
        return false; // do not break/trap
    }
}

TEST_CASE("debug: assert handler hook")
{
    AssertHandler previous = GetAssertHandler();
    SetAssertHandler(&RecordingHandler);
    g_assertCount = 0;

    RAPTOR_ASSERT(true);            // passes -> no report
    CHECK(g_assertCount == 0);

    RAPTOR_ASSERT(1 + 1 == 3);      // fails -> one report (no trap)
    CHECK(g_assertCount == 1);

    RAPTOR_ASSERT_MSG(false, "explanatory message");
    CHECK(g_assertCount == 2);

    SetAssertHandler(previous);
}

TEST_CASE("debug: RAPTOR_ENSURE returns the condition and reports on failure")
{
    AssertHandler previous = GetAssertHandler();
    SetAssertHandler(&RecordingHandler);
    g_assertCount = 0;

    CHECK(RAPTOR_ENSURE(true));     // true, no report
    CHECK(g_assertCount == 0);

    CHECK_FALSE(RAPTOR_ENSURE(false)); // false, one report
    CHECK(g_assertCount == 1);

    SetAssertHandler(previous);
}

TEST_CASE("debug: Result::Value() asserts on the error case")
{
    AssertHandler previous = GetAssertHandler();
    SetAssertHandler(&RecordingHandler);
    g_assertCount = 0;

    Result<int> r = Err(ErrorCode::NotFound);
    (void)r.Value();                // precondition violated -> reported, no trap
    CHECK(g_assertCount == 1);

    SetAssertHandler(previous);
}
