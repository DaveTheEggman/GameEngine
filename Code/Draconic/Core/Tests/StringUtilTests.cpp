// Draconic Core - :string_util tests: character classification and whitespace trimming.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;

namespace core = draconic::core;

TEST_CASE("string-util: IsWhiteSpace / IsDigit / IsHexDigit")
{
    CHECK(core::IsWhiteSpace(u8' '));
    CHECK(core::IsWhiteSpace(u8'\t'));
    CHECK(core::IsWhiteSpace(u8'\n'));
    CHECK_FALSE(core::IsWhiteSpace(u8'x'));

    CHECK(core::IsDigit(u8'0'));
    CHECK(core::IsDigit(u8'9'));
    CHECK_FALSE(core::IsDigit(u8'a'));

    CHECK(core::IsHexDigit(u8'0'));
    CHECK(core::IsHexDigit(u8'f'));
    CHECK(core::IsHexDigit(u8'F'));
    CHECK_FALSE(core::IsHexDigit(u8'g'));
}

TEST_CASE("string-util: HexValue")
{
    CHECK(core::HexValue(u8'0') == 0);
    CHECK(core::HexValue(u8'9') == 9);
    CHECK(core::HexValue(u8'a') == 10);
    CHECK(core::HexValue(u8'F') == 15);
    CHECK(core::HexValue(u8'z') == -1);
}

TEST_CASE("string-util: Trim / TrimStart / TrimEnd")
{
    CHECK(core::Trim(core::StringView(u8"  hi  ")) == core::StringView(u8"hi"));
    CHECK(core::Trim(core::StringView(u8"\t\n x \r")) == core::StringView(u8"x"));
    CHECK(core::Trim(core::StringView(u8"nospace")) == core::StringView(u8"nospace"));
    CHECK(core::Trim(core::StringView(u8"   ")).Size() == 0); // all whitespace -> empty
    CHECK(core::Trim(core::StringView(u8"")).Size() == 0);

    CHECK(core::TrimStart(core::StringView(u8"  hi  ")) == core::StringView(u8"hi  "));
    CHECK(core::TrimEnd(core::StringView(u8"  hi  ")) == core::StringView(u8"  hi"));

    // Internal whitespace is preserved.
    CHECK(core::Trim(core::StringView(u8"  a b c  ")) == core::StringView(u8"a b c"));
}
