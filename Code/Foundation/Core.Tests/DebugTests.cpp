// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;

using namespace foundation::core;

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

    DIAGNOSTIC_ASSERT(true); // passes -> no report
    CHECK(g_assertCount == 0);

    DIAGNOSTIC_ASSERT(1 + 1 == 3); // fails -> one report (no trap)
    CHECK(g_assertCount == 1);

    DIAGNOSTIC_ASSERT_MSG(false, "explanatory message");
    CHECK(g_assertCount == 2);

    SetAssertHandler(previous);
}

TEST_CASE("debug: DIAGNOSTIC_ENSURE returns the condition and reports on failure")
{
    AssertHandler previous = GetAssertHandler();
    SetAssertHandler(&RecordingHandler);
    g_assertCount = 0;

    CHECK(DIAGNOSTIC_ENSURE(true)); // true, no report
    CHECK(g_assertCount == 0);

    CHECK_FALSE(DIAGNOSTIC_ENSURE(false)); // false, one report
    CHECK(g_assertCount == 1);

    SetAssertHandler(previous);
}

TEST_CASE("debug: Result::Value() asserts on the error case")
{
    AssertHandler previous = GetAssertHandler();
    SetAssertHandler(&RecordingHandler);
    g_assertCount = 0;

    Result<int> r = Err(ErrorCode::NotFound);
    (void)r.Value(); // precondition violated -> reported, no trap
    CHECK(g_assertCount == 1);

    SetAssertHandler(previous);
}
