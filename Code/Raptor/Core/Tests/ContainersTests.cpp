#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

// --- Containers: Span ------------------------------------------------------

TEST_CASE("containers: Span views contiguous memory")
{
    int values[5] = { 10, 20, 30, 40, 50 };
    Span<int> s = values;

    CHECK(s.Size() == 5u);
    CHECK_FALSE(s.IsEmpty());
    CHECK(s[0] == 10);
    CHECK(s.Front() == 10);
    CHECK(s.Back() == 50);

    Span<int> mid = s.SubSpan(1, 3);
    CHECK(mid.Size() == 3u);
    CHECK(mid[0] == 20);

    int sum = 0;
    for (int v : s) { sum += v; }
    CHECK(sum == 150);
}

// --- Containers: Array -----------------------------------------------------

TEST_CASE("containers: Array push/access/grow")
{
    Array<int> a;
    CHECK(a.IsEmpty());

    for (int i = 0; i < 100; ++i)
    {
        a.PushBack(i);
    }
    CHECK(a.Size() == 100u);
    CHECK(a.Capacity() >= 100u);
    CHECK(a.Front() == 0);
    CHECK(a.Back() == 99);
    CHECK(a[50] == 50);

    int sum = 0;
    for (int v : a) { sum += v; }
    CHECK(sum == 4950);
}

TEST_CASE("containers: Array emplace, pop, remove")
{
    Array<int> a;
    a.EmplaceBack(1);
    a.EmplaceBack(2);
    a.EmplaceBack(3);
    a.EmplaceBack(4);

    a.RemoveAt(1);              // -> {1, 3, 4}
    CHECK(a.Size() == 3u);
    CHECK(a[0] == 1);
    CHECK(a[1] == 3);
    CHECK(a[2] == 4);

    a.RemoveAtSwap(0);          // -> {4, 3}
    CHECK(a.Size() == 2u);
    CHECK(a[0] == 4);
    CHECK(a[1] == 3);

    a.PopBack();               // -> {4}
    CHECK(a.Size() == 1u);
    CHECK(a.Back() == 4);
}

TEST_CASE("containers: Array resize and clear")
{
    Array<int> a;
    a.Resize(4);                // default-constructed ints (0)
    CHECK(a.Size() == 4u);
    CHECK(a[0] == 0);
    CHECK(a[3] == 0);

    a[2] = 99;
    a.Resize(2);                // truncate
    CHECK(a.Size() == 2u);

    const usize capBefore = a.Capacity();
    a.Clear();
    CHECK(a.IsEmpty());
    CHECK(a.Capacity() == capBefore); // clear keeps capacity
}

TEST_CASE("containers: Array manages non-trivial element lifetimes")
{
    struct Item
    {
        static int& Live() { static int n = 0; return n; }
        int value;
        explicit Item(int v) : value(v) { ++Live(); }
        Item(const Item& o) : value(o.value) { ++Live(); }
        Item(Item&& o) noexcept : value(o.value) { ++Live(); }
        Item& operator=(const Item&) = default;
        Item& operator=(Item&&) = default;
        ~Item() { --Live(); }
    };

    Item::Live() = 0;
    {
        Array<Item> a;
        for (int i = 0; i < 20; ++i) { a.EmplaceBack(i); } // forces reallocations
        CHECK(Item::Live() == 20);

        Array<Item> copy = a;       // deep copy
        CHECK(Item::Live() == 40);

        Array<Item> moved = Move(a); // steals buffer, no new Items
        CHECK(Item::Live() == 40);
        CHECK(moved.Size() == 20u);
    }
    CHECK(Item::Live() == 0); // everything destroyed
}

TEST_CASE("containers: Array honours a custom allocator")
{
    alignas(64) byte buffer[4096];
    LinearAllocator arena(buffer, sizeof(buffer));

    Array<int> a(arena);
    for (int i = 0; i < 10; ++i) { a.PushBack(i); }
    CHECK(a.Size() == 10u);
    CHECK(arena.Used() > 0u);
}

// --- Containers: String ----------------------------------------------------

TEST_CASE("string: StringView basics")
{
    StringView v = u"hello";
    CHECK(v.Size() == 5u);
    CHECK_FALSE(v.IsEmpty());
    CHECK(v[0] == u'h');
    CHECK(v == StringView(u"hello"));
    CHECK_FALSE(v == StringView(u"world"));

    CHECK(v.StartsWith(u"he"));
    CHECK(v.EndsWith(u"lo"));
    CHECK(v.SubStr(1, 3) == StringView(u"ell"));

    static_assert(CStringLength(u"abc") == 3u);
}

TEST_CASE("string: construct, append, compare")
{
    String s = u"foo";
    CHECK(s.Size() == 3u);
    CHECK(s == u"foo");

    s += u"bar";
    CHECK(s == u"foobar");
    CHECK(s.Size() == 6u);

    s.PushBack(u'!');
    CHECK(s == u"foobar!");

    // CStr is null-terminated.
    CHECK(s.CStr()[s.Size()] == u'\0');

    String empty;
    CHECK(empty.IsEmpty());
    CHECK(empty.CStr()[0] == u'\0'); // valid even with no allocation
}

TEST_CASE("string: copy and move")
{
    String a = u"original";
    String b = a;                 // deep copy
    CHECK(a == b);

    b += u"-modified";
    CHECK_FALSE(a == b);
    CHECK(a == u"original");

    String c = Move(a);           // steal buffer
    CHECK(c == u"original");
    CHECK(a.IsEmpty());
}

TEST_CASE("string: growth across reallocations")
{
    String s;
    for (int i = 0; i < 1000; ++i)
    {
        s.PushBack(u'x');
    }
    CHECK(s.Size() == 1000u);
    CHECK(s.Capacity() >= 1000u);
    CHECK(s[0] == u'x');
    CHECK(s[999] == u'x');
    CHECK(s.CStr()[1000] == u'\0');
}

TEST_CASE("string: UTF8String is the secondary type")
{
    UTF8String s = u8"utf8";
    CHECK(s.Size() == 4u);
    s += u8"-data";
    CHECK(s == u8"utf8-data");

    // widechar is 2 bytes, utf8char is 1.
    CHECK(sizeof(String::ValueType) == 2u);
    CHECK(sizeof(UTF8String::ValueType) == 1u);
}

TEST_CASE("string: honours a custom allocator")
{
    alignas(64) byte buffer[2048];
    LinearAllocator arena(buffer, sizeof(buffer));

    String s(arena);
    s += u"arena-backed string";
    CHECK(s == u"arena-backed string");
    CHECK(arena.Used() > 0u);
}

// --- Hash ------------------------------------------------------------------

TEST_CASE("hash: integers and strings hash deterministically")
{
    Hash<int> hi;
    CHECK(hi(42) == hi(42));
    CHECK(hi(42) != hi(43));

    Hash<StringView> hs;
    CHECK(hs(u"hello") == hs(u"hello"));
    CHECK(hs(u"hello") != hs(u"world"));

    CHECK(HashBytes("abc", 3) == HashBytes("abc", 3));
}

// --- Containers: HashMap ---------------------------------------------------

TEST_CASE("hashmap: insert, find, contains, overwrite")
{
    HashMap<int, int> m;
    CHECK(m.IsEmpty());

    m.InsertOrAssign(1, 100);
    m.InsertOrAssign(2, 200);
    CHECK(m.Size() == 2u);
    CHECK(m.Contains(1));
    CHECK_FALSE(m.Contains(99));

    REQUIRE(m.Find(2) != nullptr);
    CHECK(*m.Find(2) == 200);
    CHECK(m.Find(99) == nullptr);

    m.InsertOrAssign(1, 111); // overwrite
    CHECK(m.Size() == 2u);
    CHECK(*m.Find(1) == 111);
}

TEST_CASE("hashmap: remove and tombstone reuse")
{
    HashMap<int, int> m;
    for (int i = 0; i < 50; ++i) { m.InsertOrAssign(i, i * 10); }
    CHECK(m.Size() == 50u);

    CHECK(m.Remove(25));
    CHECK_FALSE(m.Remove(25)); // already gone
    CHECK(m.Size() == 49u);
    CHECK_FALSE(m.Contains(25));

    m.InsertOrAssign(25, 999); // reuse
    CHECK(m.Contains(25));
    CHECK(*m.Find(25) == 999);

    // All other keys still findable after rehashes.
    for (int i = 0; i < 50; ++i)
    {
        REQUIRE(m.Find(i) != nullptr);
        CHECK(*m.Find(i) == (i == 25 ? 999 : i * 10));
    }
}

TEST_CASE("hashmap: grows and iterates")
{
    HashMap<int, int> m;
    int expectedSum = 0;
    for (int i = 0; i < 500; ++i)
    {
        m.InsertOrAssign(i, i);
        expectedSum += i;
    }
    CHECK(m.Size() == 500u);

    int sum = 0;
    int count = 0;
    for (auto& entry : m)
    {
        sum += entry.value;
        ++count;
    }
    CHECK(count == 500);
    CHECK(sum == expectedSum);
}

TEST_CASE("hashmap: string keys")
{
    HashMap<String, int> ages;
    ages.InsertOrAssign(String(u"alice"), 30);
    ages.InsertOrAssign(String(u"bob"), 25);

    REQUIRE(ages.Find(String(u"alice")) != nullptr);
    CHECK(*ages.Find(String(u"alice")) == 30);
    CHECK(*ages.Find(String(u"bob")) == 25);
    CHECK(ages.Find(String(u"carol")) == nullptr);
}

TEST_CASE("hashmap: manages non-trivial value lifetimes")
{
    struct Val
    {
        static int& Live() { static int n = 0; return n; }
        int v;
        explicit Val(int x = 0) : v(x) { ++Live(); }
        Val(const Val& o) : v(o.v) { ++Live(); }
        Val(Val&& o) noexcept : v(o.v) { ++Live(); }
        Val& operator=(const Val&) = default;
        Val& operator=(Val&&) = default;
        ~Val() { --Live(); }
    };

    Val::Live() = 0;
    {
        HashMap<int, Val> m;
        for (int i = 0; i < 30; ++i) { m.InsertOrAssign(i, Val{ i }); } // forces rehashes
        CHECK(m.Size() == 30u);
        m.Remove(5);
        CHECK(m.Size() == 29u);
    }
    CHECK(Val::Live() == 0); // all destroyed across rehash/remove/clear
}
