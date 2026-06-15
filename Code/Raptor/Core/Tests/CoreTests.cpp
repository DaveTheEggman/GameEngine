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

// --- Memory ----------------------------------------------------------------

TEST_CASE("memory: alignment helpers")
{
    CHECK(IsPowerOfTwo(1u));
    CHECK(IsPowerOfTwo(64u));
    CHECK_FALSE(IsPowerOfTwo(0u));
    CHECK_FALSE(IsPowerOfTwo(48u));

    CHECK(AlignUp(0u, 16u) == 0u);
    CHECK(AlignUp(1u, 16u) == 16u);
    CHECK(AlignUp(16u, 16u) == 16u);
    CHECK(AlignUp(17u, 16u) == 32u);
    CHECK(AlignDown(31u, 16u) == 16u);

    static_assert(AlignUp(17u, 16u) == 32u);
}

TEST_CASE("memory: SystemAllocator gives aligned, usable storage")
{
    IAllocator& alloc = DefaultAllocator();

    void* p = alloc.Allocate(128, 64);
    REQUIRE(p != nullptr);
    CHECK(IsAligned(p, 64));

    MemSet(p, 0xAB, 128);
    CHECK(static_cast<u8*>(p)[0] == 0xABu);
    CHECK(static_cast<u8*>(p)[127] == 0xABu);

    alloc.Free(p);
    alloc.Free(nullptr); // no-op
}

TEST_CASE("memory: New / Delete construct and destroy")
{
    struct Tracked
    {
        static int& Live() { static int n = 0; return n; }
        int value;
        explicit Tracked(int v) : value(v) { ++Live(); }
        ~Tracked() { --Live(); }
    };

    IAllocator& alloc = DefaultAllocator();
    CHECK(Tracked::Live() == 0);

    Tracked* t = alloc.New<Tracked>(7);
    REQUIRE(t != nullptr);
    CHECK(t->value == 7);
    CHECK(Tracked::Live() == 1);

    alloc.Delete(t);
    CHECK(Tracked::Live() == 0);
}

TEST_CASE("memory: LinearAllocator bumps, aligns, exhausts, resets")
{
    alignas(64) byte buffer[256];
    LinearAllocator arena(buffer, sizeof(buffer));

    CHECK(arena.Capacity() == 256u);
    CHECK(arena.Used() == 0u);

    void* a = arena.Allocate(10, 16);
    REQUIRE(a != nullptr);
    CHECK(IsAligned(a, 16));

    void* b = arena.Allocate(10, 16);
    REQUIRE(b != nullptr);
    CHECK(IsAligned(b, 16));
    CHECK(b != a);
    CHECK(arena.Used() >= 20u);

    // Free is a no-op; the arena keeps growing.
    arena.Free(a);

    // Exhaust it.
    void* big = arena.Allocate(1024, 16);
    CHECK(big == nullptr);

    arena.Reset();
    CHECK(arena.Used() == 0u);
    void* c = arena.Allocate(10, 16);
    CHECK(c == a); // first allocation lands at the start again
}

// --- Smart pointers --------------------------------------------------------

namespace
{
    struct Widget : RefCounted
    {
        static int& Live() { static int n = 0; return n; }
        int value;
        explicit Widget(int v) : value(v) { ++Live(); }
        ~Widget() override { --Live(); }
    };

    struct DerivedWidget : Widget
    {
        explicit DerivedWidget(int v) : Widget(v) {}
    };
}

TEST_CASE("memory: UniquePtr owns and releases")
{
    Widget::Live() = 0;
    {
        UniquePtr<Widget> u = MakeUnique<Widget>(DefaultAllocator(), 5);
        REQUIRE(static_cast<bool>(u));
        CHECK(u->value == 5);
        CHECK(Widget::Live() == 1);

        UniquePtr<Widget> moved = Move(u);
        CHECK_FALSE(static_cast<bool>(u));
        CHECK(moved->value == 5);
        CHECK(Widget::Live() == 1);
    }
    CHECK(Widget::Live() == 0);
}

TEST_CASE("memory: RefPtr shares ownership via the intrusive count")
{
    Widget::Live() = 0;
    {
        RefPtr<Widget> a = MakeRef<Widget>(DefaultAllocator(), 9);
        REQUIRE(static_cast<bool>(a));
        CHECK(a->value == 9);
        CHECK(a->RefCount() == 1u);
        CHECK(Widget::Live() == 1);

        {
            RefPtr<Widget> b = a; // copy -> +1
            CHECK(a->RefCount() == 2u);
            CHECK(b.Get() == a.Get());
            CHECK(a == b);
        }
        // b dropped -> back to 1
        CHECK(a->RefCount() == 1u);
        CHECK(Widget::Live() == 1);
    }
    CHECK(Widget::Live() == 0); // destroyed at strong -> 0
}

TEST_CASE("memory: RefPtr upcasts from a derived type")
{
    Widget::Live() = 0;
    {
        RefPtr<DerivedWidget> d = MakeRef<DerivedWidget>(DefaultAllocator(), 3);
        RefPtr<Widget> base = d; // upcast, shares the count
        CHECK(base->value == 3);
        CHECK(d->RefCount() == 2u);
    }
    CHECK(Widget::Live() == 0);
}

TEST_CASE("memory: WeakRefPtr locks while alive and expires after")
{
    Widget::Live() = 0;

    WeakRefPtr<Widget> weak;
    CHECK(weak.Expired());

    {
        RefPtr<Widget> strong = MakeRef<Widget>(DefaultAllocator(), 11);
        weak = WeakRefPtr<Widget>(strong);

        CHECK_FALSE(weak.Expired());
        CHECK(Widget::Live() == 1);

        RefPtr<Widget> locked = weak.Lock();
        REQUIRE(static_cast<bool>(locked));
        CHECK(locked->value == 11);
        CHECK(strong->RefCount() == 2u); // strong + locked
    }

    // strong gone -> object destroyed, but the weak ref keeps the control block.
    CHECK(Widget::Live() == 0);
    CHECK(weak.Expired());
    CHECK_FALSE(static_cast<bool>(weak.Lock()));
}

TEST_CASE("memory: outstanding WeakRefPtr does not keep the object alive")
{
    Widget::Live() = 0;

    WeakRefPtr<Widget> weak;
    {
        RefPtr<Widget> strong = MakeRef<Widget>(DefaultAllocator(), 1);
        weak = WeakRefPtr<Widget>(strong);
        CHECK(Widget::Live() == 1);
    }
    // Object destroyed at strong -> 0 even though a weak ref remains.
    CHECK(Widget::Live() == 0);
    CHECK(weak.Expired());
    // weak destructor here frees the retained control block (clean under ASan).
}

// --- System ----------------------------------------------------------------

TEST_CASE("system: high-resolution time advances")
{
    CHECK(GetTickFrequency() > 0u);

    const u64 t0 = GetTicks();
    SleepMilliseconds(2);
    const u64 t1 = GetTicks();

    CHECK(t1 > t0);

    const f64 seconds = TicksToSeconds(t1 - t0);
    CHECK(seconds > 0.0);
    CHECK(seconds < 1.0); // a 2ms sleep should be well under a second
    CHECK(TicksToMilliseconds(t1 - t0) >= 1.0);
}

TEST_CASE("system: info queries are sane")
{
    CHECK(LogicalCoreCount() >= 1u);

    const usize pageSize = PageSize();
    CHECK(pageSize >= 4096u);
    CHECK(IsPowerOfTwo(pageSize));
}

TEST_CASE("system: page allocation is usable and page-aligned")
{
    const usize pageSize = PageSize();

    void* p = PageAllocate(pageSize);
    REQUIRE(p != nullptr);
    CHECK(IsAligned(p, pageSize));

    MemSet(p, 0x5A, pageSize);
    CHECK(static_cast<u8*>(p)[0] == 0x5Au);
    CHECK(static_cast<u8*>(p)[pageSize - 1] == 0x5Au);

    PageFree(p, pageSize);
}

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

// --- System: files ---------------------------------------------------------

TEST_CASE("system: file write / read / seek / size round-trip")
{
    const char* path = "raptor_system_test.tmp";
    const char payload[] = "Raptor file IO";
    const u64 length = sizeof(payload) - 1; // exclude null terminator

    // Write
    {
        FileHandle f = FileOpen(path, FileMode::Write);
        REQUIRE(FileIsValid(f));
        CHECK(FileWrite(f, payload, length) == static_cast<i64>(length));
        FileClose(f);
    }

    CHECK(FileExists(path));

    // Read back
    {
        FileHandle f = FileOpen(path, FileMode::Read);
        REQUIRE(FileIsValid(f));
        CHECK(FileSize(f) == static_cast<i64>(length));

        char buffer[32] = {};
        CHECK(FileRead(f, buffer, length) == static_cast<i64>(length));
        CHECK(buffer[0] == 'R');

        // Seek back to a known offset and re-read.
        CHECK(FileSeek(f, 7, SeekOrigin::Begin) == 7);
        char c = 0;
        CHECK(FileRead(f, &c, 1) == 1);
        CHECK(c == 'f'); // "Raptor file IO"[7]

        FileClose(f);
    }

    CHECK(FileDelete(path));
    CHECK_FALSE(FileExists(path));
}

TEST_CASE("system: opening a missing file fails cleanly")
{
    FileHandle f = FileOpen("raptor_definitely_missing.xyz", FileMode::Read);
    CHECK_FALSE(FileIsValid(f));
}

TEST_CASE("system: console write does not crash")
{
    const char msg[] = "[raptor-test] console output check\n";
    ConsoleWrite(msg, sizeof(msg) - 1);
    CHECK(true);
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
