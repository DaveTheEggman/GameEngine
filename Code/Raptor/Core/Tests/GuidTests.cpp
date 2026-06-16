#include <doctest/doctest.h>

#include "Core/Prelude.h"  // brings <new> into reach for container instantiation (GCC)

import raptor.core;

using namespace raptor::core;

TEST_CASE("guid: nil and default")
{
    CHECK(Guid{}.IsNil());
    CHECK(Guid::Nil.IsNil());
    CHECK(!static_cast<bool>(Guid::Nil));

    const Guid g{ 1, 2 };
    CHECK(!g.IsNil());
    CHECK(static_cast<bool>(g));
}

TEST_CASE("guid: generation is deterministic per seed and v4-tagged")
{
    Random a(777);
    Random b(777);
    Random c(778);

    const Guid ga = Guid::Generate(a);
    const Guid gb = Guid::Generate(b);
    const Guid gc = Guid::Generate(c);

    CHECK(ga == gb);   // same seed -> same id
    CHECK(ga != gc);   // different seed -> different id
    CHECK(!ga.IsNil());

    // Version nibble (4) and variant bits (10xx) per RFC 4122.
    CHECK(((ga.high >> 12) & 0xF) == 0x4);
    CHECK(((ga.low >> 62) & 0x3) == 0x2);
}

TEST_CASE("guid: ToChars / TryParse round-trip")
{
    Random rng(2025);
    const Guid g = Guid::Generate(rng);

    char text[37];
    g.ToChars(text);

    // Canonical layout: 8-4-4-4-12 with dashes at fixed positions.
    CHECK(text[8] == '-');
    CHECK(text[13] == '-');
    CHECK(text[18] == '-');
    CHECK(text[23] == '-');
    CHECK(text[36] == '\0');

    Guid parsed{};
    CHECK(Guid::TryParse(text, parsed));
    CHECK(parsed == g);
}

TEST_CASE("guid: TryParse rejects malformed input")
{
    Guid out{ 9, 9 };
    CHECK(!Guid::TryParse(nullptr, out));
    CHECK(!Guid::TryParse("not-a-guid", out));
    CHECK(!Guid::TryParse("00000000-0000-0000-0000-00000000000", out));   // too short
    CHECK(!Guid::TryParse("00000000+0000-0000-0000-000000000000", out));  // wrong separator
    CHECK(!Guid::TryParse("0000000g-0000-0000-0000-000000000000", out));  // non-hex
    CHECK(out == Guid{ 9, 9 });  // unchanged on failure

    // Accepts uppercase hex.
    Guid ok{};
    CHECK(Guid::TryParse("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF", ok));
    CHECK(ok.high == 0xFFFFFFFFFFFFFFFFull);
    CHECK(ok.low == 0xFFFFFFFFFFFFFFFFull);
}

TEST_CASE("guid: usable as a hashed-container key")
{
    Random rng(1);
    HashSet<Guid> set;
    Guid first{};
    for (int i = 0; i < 64; ++i)
    {
        const Guid g = Guid::Generate(rng);
        if (i == 0) { first = g; }
        set.Insert(g);
    }
    CHECK(set.Size() == 64);     // all distinct
    CHECK(set.Contains(first));
    CHECK(!set.Contains(Guid::Nil));
}
