// Ported from Sedulous.UI.Tests/src/InputFilterTests.bf (faithful; Beef `scope`/`new` -> value,
// delegate -> lambda). char literals are char32_t (U'...').
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
namespace core = draconic::core;

TEST_CASE("input-filter: None_AcceptsAll")
{
    InputFilter filter;
    CHECK(filter.Accept(U'a'));
    CHECK(filter.Accept(U'Z'));
    CHECK(filter.Accept(U'5'));
    CHECK(filter.Accept(U' '));
    CHECK(filter.Accept(U'!'));
}

TEST_CASE("input-filter: Digits_AcceptsOnlyDigits")
{
    InputFilter filter = InputFilter::Digits();
    CHECK(filter.Accept(U'0'));
    CHECK(filter.Accept(U'5'));
    CHECK(filter.Accept(U'9'));
    CHECK(!filter.Accept(U'a'));
    CHECK(!filter.Accept(U' '));
    CHECK(!filter.Accept(U'.'));
}

TEST_CASE("input-filter: HexDigits_AcceptsHexChars")
{
    InputFilter filter = InputFilter::HexDigits();
    CHECK(filter.Accept(U'0'));
    CHECK(filter.Accept(U'9'));
    CHECK(filter.Accept(U'a'));
    CHECK(filter.Accept(U'f'));
    CHECK(filter.Accept(U'A'));
    CHECK(filter.Accept(U'F'));
    CHECK(!filter.Accept(U'g'));
    CHECK(!filter.Accept(U'G'));
    CHECK(!filter.Accept(U' '));
}

TEST_CASE("input-filter: Custom_UsesDelegate")
{
    InputFilter filter;
    filter.SetCustomFilter([](char32_t c) { return c == U'x' || c == U'y'; });
    CHECK(filter.Accept(U'x'));
    CHECK(filter.Accept(U'y'));
    CHECK(!filter.Accept(U'z'));
    CHECK(!filter.Accept(U'a'));
}
