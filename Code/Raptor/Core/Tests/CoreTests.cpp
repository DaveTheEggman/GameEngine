// Raptor::Core — smoke tests.
//
// Primary purpose at Phase 0: prove `import raptor.core;` compiles, links, and
// runs. Grows into real per-subsystem coverage as Core fills out.

#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

// --- A small reflected hierarchy for the RTTI tests ------------------------
namespace
{
    class Animal : public Object
    {
        RAPTOR_OBJECT(Animal, Object)
    public:
        int legs = 4;

        int AddLegs(int n) { legs += n; return legs; }
        int GetLegs() const { return legs; }
        static int DefaultLegs() { return 4; }
    };

    class Dog : public Animal
    {
        RAPTOR_OBJECT(Dog, Animal)
    public:
        const char* Speak() const { return "woof"; }
    };

    class Cat : public Animal
    {
        RAPTOR_OBJECT(Cat, Animal)
    };
}

RAPTOR_REFLECT(Animal, "raptor::test")
{
    builder.Property<&Animal::legs>("legs");
    builder.Method<&Animal::AddLegs>("AddLegs");
    builder.Method<&Animal::GetLegs>("GetLegs");
    builder.Method<&Animal::DefaultLegs>("DefaultLegs");
    builder.Attribute("scriptName", "Critter");
    builder.Attribute("maxLegs", 8);
}
RAPTOR_DEFINE_OBJECT(Dog, "raptor::test")
RAPTOR_DEFINE_OBJECT(Cat, "raptor::test")

enum class Color : int { Red = 1, Green = 2, Blue = 4 };

RAPTOR_REFLECT_ENUM(Color, "raptor::test")
{
    builder.Value("Red", Color::Red);
    builder.Value("Green", Color::Green);
    builder.Value("Blue", Color::Blue);
}

namespace
{
    // Builds a formatted string and returns whether it equals `expected`.
    template <typename... Args>
    bool FormatEquals(const char* expected, const char* fmt, const Args&... args)
    {
        FormatBuffer buffer;
        FormatTo(buffer, fmt, args...);
        return std::strcmp(buffer.Data(), expected) == 0;
    }
}

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

TEST_CASE("memory: PoolAllocator hands out and recycles fixed blocks")
{
    alignas(16) byte buffer[256];
    PoolAllocator pool(buffer, sizeof(buffer), 32, 16);

    const usize capacity = pool.Capacity();
    CHECK(capacity >= 1u);
    CHECK(pool.FreeCount() == capacity);
    CHECK(pool.BlockSize() >= 32u);

    // Drain the pool.
    Array<void*> blocks;
    for (usize i = 0; i < capacity; ++i)
    {
        void* b = pool.Allocate(32, 16);
        REQUIRE(b != nullptr);
        CHECK(IsAligned(b, 16));
        blocks.PushBack(b);
    }
    CHECK(pool.FreeCount() == 0u);
    CHECK(pool.Allocate(32, 16) == nullptr); // exhausted

    // Free one and reallocate -> recycles the block.
    void* recycled = blocks[0];
    pool.Free(recycled);
    CHECK(pool.FreeCount() == 1u);
    CHECK(pool.Allocate(32, 16) == recycled);
}

TEST_CASE("memory: StackAllocator markers reclaim in LIFO order")
{
    alignas(16) byte buffer[256];
    StackAllocator stack(buffer, sizeof(buffer));

    void* a = stack.Allocate(16, 16);
    REQUIRE(a != nullptr);

    const StackAllocator::Marker marker = stack.GetMarker();
    void* b = stack.Allocate(32, 16);
    void* c = stack.Allocate(32, 16);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(stack.Used() >= 80u);

    // Roll back to the marker; the next allocation reuses b's address.
    stack.FreeToMarker(marker);
    void* b2 = stack.Allocate(32, 16);
    CHECK(b2 == b);

    stack.Reset();
    CHECK(stack.Used() == 0u);
    CHECK(stack.Allocate(16, 16) == a);
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

// --- Format ----------------------------------------------------------------

TEST_CASE("format: substitution and types")
{
    CHECK(FormatEquals("no args", "no args"));
    CHECK(FormatEquals("a=1 b=2", "a={} b={}", 1, 2));
    CHECK(FormatEquals("neg -42", "neg {}", -42));
    CHECK(FormatEquals("big 4294967295", "big {}", 4294967295u));
    CHECK(FormatEquals("flag true and false", "flag {} and {}", true, false));
    CHECK(FormatEquals("char X", "char {}", 'X'));
    CHECK(FormatEquals("str hello", "str {}", "hello"));
    CHECK(FormatEquals("pi 3.5", "pi {}", 3.5));
}

TEST_CASE("format: brace escapes and extra/missing args")
{
    CHECK(FormatEquals("{literal}", "{{literal}}"));
    CHECK(FormatEquals("set {x} = 7", "set {{x}} = {}", 7));
    CHECK(FormatEquals("only 1", "only {}", 1, 2, 3)); // extra args ignored
    CHECK(FormatEquals("missing {}", "missing {}"));    // unmatched placeholder left as-is
}

// --- Log -------------------------------------------------------------------

namespace
{
    struct CapturingSink : ILogSink
    {
        int count = 0;
        LogLevel lastLevel = LogLevel::Off;
        char lastCategory[64] = {};
        char lastMessage[256] = {};

        void Write(LogLevel level, const char* category,
                   const char* message, usize length) noexcept override
        {
            ++count;
            lastLevel = level;
            std::strncpy(lastCategory, category, sizeof(lastCategory) - 1);
            const usize n = (length < sizeof(lastMessage) - 1) ? length : sizeof(lastMessage) - 1;
            std::memcpy(lastMessage, message, n);
            lastMessage[n] = '\0';
        }
    };
}

TEST_CASE("log: dispatch, formatting, and level filtering")
{
    CapturingSink sink;
    Logger& logger = GlobalLogger();
    const LogLevel previousLevel = logger.MinLevel();

    logger.AddSink(&sink);
    logger.SetMinLevel(LogLevel::Info);

    RAPTOR_LOG_INFO("Renderer", "loaded {} meshes", 12);
    CHECK(sink.count == 1);
    CHECK(sink.lastLevel == LogLevel::Info);
    CHECK(std::strcmp(sink.lastCategory, "Renderer") == 0);
    CHECK(std::strcmp(sink.lastMessage, "loaded 12 meshes") == 0);

    // Below the min level -> filtered out.
    RAPTOR_LOG_DEBUG("Renderer", "verbose {}", 1);
    CHECK(sink.count == 1);

    // At/above min level -> delivered.
    RAPTOR_LOG_ERROR("Audio", "device {} lost", 3);
    CHECK(sink.count == 2);
    CHECK(sink.lastLevel == LogLevel::Error);
    CHECK(std::strcmp(sink.lastMessage, "device 3 lost") == 0);

    logger.RemoveSink(&sink);
    logger.SetMinLevel(previousLevel);

    // After removal, no more delivery.
    RAPTOR_LOG_ERROR("Audio", "ignored");
    CHECK(sink.count == 2);
}

// --- Math: scalars ---------------------------------------------------------

TEST_CASE("math: scalar helpers")
{
    CHECK(Abs(-3.0f) == 3.0f);
    CHECK(NearlyEqual(DegreesToRadians(180.0f), kPi));
    CHECK(NearlyEqual(RadiansToDegrees(kPi), 180.0f));
    CHECK(NearlyEqual(Lerp(0.0f, 10.0f, 0.25f), 2.5f));
    CHECK(NearlyEqual(Sqrt(16.0f), 4.0f));
    CHECK(NearlyZero(1.0e-8f));

    static_assert(Abs(-1.0f) == 1.0f);
}

// --- Math: Vec3 ------------------------------------------------------------

TEST_CASE("math: Vec3 arithmetic")
{
    Vec3 a{ 1.0f, 2.0f, 3.0f };
    Vec3 b{ 4.0f, 5.0f, 6.0f };

    CHECK((a + b) == Vec3{ 5.0f, 7.0f, 9.0f });
    CHECK((b - a) == Vec3{ 3.0f, 3.0f, 3.0f });
    CHECK((a * 2.0f) == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK((2.0f * a) == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK((-a) == Vec3{ -1.0f, -2.0f, -3.0f });

    a += b;
    CHECK(a == Vec3{ 5.0f, 7.0f, 9.0f });

    CHECK(a[0] == 5.0f);
    CHECK(a[2] == 9.0f);

    static_assert(Vec3{ 1.0f, 0.0f, 0.0f } == Vec3::UnitX);
}

TEST_CASE("math: Vec3 dot, cross, length, normalize")
{
    CHECK(Dot(Vec3{ 1.0f, 2.0f, 3.0f }, Vec3{ 4.0f, 5.0f, 6.0f }) == 32.0f);

    // Right-handed cross: X x Y = Z
    CHECK(Cross(Vec3::UnitX, Vec3::UnitY) == Vec3::UnitZ);
    CHECK(Cross(Vec3::UnitY, Vec3::UnitZ) == Vec3::UnitX);

    CHECK(LengthSquared(Vec3{ 3.0f, 4.0f, 0.0f }) == 25.0f);
    CHECK(NearlyEqual(Length(Vec3{ 3.0f, 4.0f, 0.0f }), 5.0f));

    Vec3 n = Normalized(Vec3{ 0.0f, 8.0f, 0.0f });
    CHECK(NearlyEqual(n, Vec3::UnitY));
    CHECK(NearlyEqual(Length(n), 1.0f));

    // Degenerate input -> Zero, no NaN/divide-by-zero.
    CHECK(Normalized(Vec3::Zero) == Vec3::Zero);
}

TEST_CASE("math: Vec3 lerp/min/max")
{
    CHECK(Lerp(Vec3::Zero, Vec3{ 4.0f, 8.0f, 12.0f }, 0.5f) == Vec3{ 2.0f, 4.0f, 6.0f });
    CHECK(Min(Vec3{ 1.0f, 5.0f, 3.0f }, Vec3{ 4.0f, 2.0f, 6.0f }) == Vec3{ 1.0f, 2.0f, 3.0f });
    CHECK(Max(Vec3{ 1.0f, 5.0f, 3.0f }, Vec3{ 4.0f, 2.0f, 6.0f }) == Vec3{ 4.0f, 5.0f, 6.0f });
}

// --- Math: Vec2 / Vec4 -----------------------------------------------------

TEST_CASE("math: Vec2 and Vec4 basics")
{
    CHECK(Dot(Vec2{ 1.0f, 2.0f }, Vec2{ 3.0f, 4.0f }) == 11.0f);
    CHECK(NearlyEqual(Length(Vec2{ 3.0f, 4.0f }), 5.0f));

    Vec4 v{ Vec3{ 1.0f, 2.0f, 3.0f }, 1.0f };
    CHECK(v.XYZ() == Vec3{ 1.0f, 2.0f, 3.0f });
    CHECK(v.w == 1.0f);
    CHECK(Dot(Vec4::One, Vec4::One) == 4.0f);
}

// --- Math: Mat4 ------------------------------------------------------------

TEST_CASE("math: Mat4 identity and multiply")
{
    const Mat4 id = Mat4::Identity();
    const Mat4 t = Mat4::Translation(Vec3{ 1.0f, 2.0f, 3.0f });

    CHECK(NearlyEqual(id * t, t));
    CHECK(NearlyEqual(t * id, t));

    static_assert(Mat4::Identity()(0, 0) == 1.0f);
    static_assert(Mat4::Identity()(0, 1) == 0.0f);
}

TEST_CASE("math: Mat4 translation lives in the last row (row vectors)")
{
    const Mat4 t = Mat4::Translation(Vec3{ 10.0f, 20.0f, 30.0f });
    CHECK(t.m[3][0] == 10.0f);
    CHECK(t.m[3][1] == 20.0f);
    CHECK(t.m[3][2] == 30.0f);

    const Vec3 p = TransformPoint(Vec3{ 1.0f, 1.0f, 1.0f }, t);
    CHECK(NearlyEqual(p, Vec3{ 11.0f, 21.0f, 31.0f }));

    // Directions ignore translation.
    CHECK(NearlyEqual(TransformDirection(Vec3{ 1.0f, 0.0f, 0.0f }, t), Vec3{ 1.0f, 0.0f, 0.0f }));
}

TEST_CASE("math: Mat4 rotation (row vectors): RotationZ(90) maps +X to +Y")
{
    const Mat4 rz = Mat4::RotationZ(DegreesToRadians(90.0f));
    CHECK(NearlyEqual(TransformDirection(Vec3::UnitX, rz), Vec3::UnitY));

    const Mat4 ry = Mat4::RotationY(DegreesToRadians(90.0f));
    // RotationY(90) maps +Z to +X.
    CHECK(NearlyEqual(TransformDirection(Vec3::UnitZ, ry), Vec3::UnitX));
}

TEST_CASE("math: Mat4 composition reads left-to-right (scale then translate)")
{
    // v * (S * T): scale first, then translate.
    const Mat4 st = Mat4::Scale(Vec3{ 2.0f, 2.0f, 2.0f }) * Mat4::Translation(Vec3{ 1.0f, 0.0f, 0.0f });
    const Vec3 p = TransformPoint(Vec3{ 1.0f, 1.0f, 1.0f }, st);
    CHECK(NearlyEqual(p, Vec3{ 3.0f, 2.0f, 2.0f })); // (2,2,2) + (1,0,0)
}

TEST_CASE("math: perspective has the expected projective structure")
{
    const Mat4 proj = Mat4::PerspectiveFovRH(DegreesToRadians(90.0f), 1.0f, 1.0f, 100.0f);
    CHECK(proj.m[2][3] == -1.0f);              // w' = -z (RH)
    CHECK(NearlyEqual(proj.m[0][0], 1.0f));    // xScale = 1/tan(45) at aspect 1
}

// --- Math: Quat ------------------------------------------------------------

TEST_CASE("math: Quat rotates vectors and agrees with its matrix")
{
    const Quat q = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(90.0f));

    // 90 deg about Z maps +X to +Y.
    CHECK(NearlyEqual(RotateVector(q, Vec3::UnitX), Vec3::UnitY));

    // Quaternion rotation and its matrix agree.
    const Mat4 r = RotationMatrix(q);
    CHECK(NearlyEqual(RotateVector(q, Vec3::UnitX), TransformDirection(Vec3::UnitX, r)));
    CHECK(NearlyEqual(RotateVector(q, Vec3{ 0.3f, -0.5f, 0.8f }),
                      TransformDirection(Vec3{ 0.3f, -0.5f, 0.8f }, r)));

    // Identity does nothing.
    CHECK(NearlyEqual(RotateVector(Quat::Identity, Vec3{ 1.0f, 2.0f, 3.0f }), Vec3{ 1.0f, 2.0f, 3.0f }));

    // Composition: two 45-deg rotations == one 90-deg.
    const Quat half = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(45.0f));
    CHECK(NearlyEqual(RotateVector(half * half, Vec3::UnitX), Vec3::UnitY));
}

// --- Math: Transform -------------------------------------------------------

TEST_CASE("math: Transform composes scale, rotation, translation")
{
    Transform xform;
    xform.scale = Vec3{ 2.0f, 2.0f, 2.0f };
    xform.rotation = Quat::FromAxisAngle(Vec3::UnitZ, DegreesToRadians(90.0f));
    xform.position = Vec3{ 5.0f, 0.0f, 0.0f };

    const Mat4 m = xform.ToMatrix();

    // (1,0,0) -> scale*2 -> (2,0,0) -> rot90Z -> (0,2,0) -> +translate -> (5,2,0)
    const Vec3 p = TransformPoint(Vec3::UnitX, m);
    CHECK(NearlyEqual(p, Vec3{ 5.0f, 2.0f, 0.0f }));

    // Identity transform is a no-op.
    Transform identity;
    CHECK(NearlyEqual(TransformPoint(Vec3{ 7.0f, 8.0f, 9.0f }, identity.ToMatrix()),
                      Vec3{ 7.0f, 8.0f, 9.0f }));
}

// --- RTTI ------------------------------------------------------------------

TEST_CASE("rtti: stable type identity")
{
    CHECK(Dog::StaticType().id == Dog::StaticType().id);
    CHECK(Dog::StaticType().id != Cat::StaticType().id);
    CHECK(Dog::StaticType().base == &Animal::StaticType());
    CHECK(Animal::StaticType().base == &Object::StaticType());
    CHECK(Object::StaticType().base == nullptr);

    CHECK(std::strcmp(Dog::StaticType().name, "Dog") == 0);
    CHECK(std::strcmp(Dog::StaticType().namespaceName, "raptor::test") == 0);
}

TEST_CASE("rtti: GetType is virtual through a base pointer")
{
    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    RefPtr<Object> asObject = dog; // upcast

    CHECK(asObject->GetType() == &Dog::StaticType());
    CHECK(dog->GetType()->id == Dog::StaticType().id);
}

TEST_CASE("rtti: Cast and IsA walk the inheritance chain")
{
    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    Object* obj = dog.Get();

    CHECK(IsA<Dog>(obj));
    CHECK(IsA<Animal>(obj)); // up the chain
    CHECK(IsA<Object>(obj));
    CHECK_FALSE(IsA<Cat>(obj));

    // Down/cross casts
    REQUIRE(Cast<Animal>(obj) != nullptr);
    REQUIRE(Cast<Dog>(obj) != nullptr);
    CHECK(Cast<Cat>(obj) == nullptr);

    // Casting preserves the object and lets us call derived API.
    Dog* backToDog = Cast<Dog>(Cast<Animal>(obj));
    REQUIRE(backToDog != nullptr);
    CHECK(std::strcmp(backToDog->Speak(), "woof") == 0);

    CHECK_FALSE(IsA<Dog>(static_cast<Object*>(nullptr)));
}

TEST_CASE("rtti: explicit registration and lookup")
{
    TypeRegistry& registry = GlobalTypeRegistry();
    const usize before = registry.Count();

    registry.Register(Object::StaticType());
    registry.Register(Animal::StaticType());
    registry.Register(Dog::StaticType());
    registry.Register(Cat::StaticType());

    CHECK(registry.Count() >= before + 4);

    CHECK(registry.FindById(Dog::StaticType().id) == &Dog::StaticType());
    CHECK(registry.FindByName("raptor::test", "Cat") == &Cat::StaticType());
    CHECK(registry.FindByName("raptor::test", "Missing") == nullptr);

    // Idempotent: re-registering does not duplicate.
    const usize count = registry.Count();
    registry.Register(Dog::StaticType());
    CHECK(registry.Count() == count);
}

// --- RTTI: Variant ---------------------------------------------------------

TEST_CASE("variant: holds small values inline")
{
    Variant v = Variant::From(42);
    CHECK_FALSE(v.IsEmpty());
    CHECK(v.Is<int>());
    CHECK_FALSE(v.Is<float>());

    REQUIRE(v.TryGet<int>() != nullptr);
    CHECK(*v.TryGet<int>() == 42);
    CHECK(v.TryGet<float>() == nullptr);
    CHECK(v.Get<int>() == 42);

    CHECK(v.Type() == &TypeOf<int>());
}

TEST_CASE("variant: holds a Vec3 and a large (heap) value")
{
    Variant small = Variant::From(Vec3{ 1.0f, 2.0f, 3.0f });
    REQUIRE(small.Is<Vec3>());
    CHECK(*small.TryGet<Vec3>() == Vec3{ 1.0f, 2.0f, 3.0f });

    Variant large = Variant::From(Mat4::Translation(Vec3{ 5.0f, 0.0f, 0.0f }));
    REQUIRE(large.Is<Mat4>());
    CHECK(large.TryGet<Mat4>()->m[3][0] == 5.0f);
}

TEST_CASE("variant: copy and move are independent")
{
    Variant a = Variant::From(7);
    Variant b = a;            // copy
    *b.TryGet<int>() = 99;
    CHECK(*a.TryGet<int>() == 7);
    CHECK(*b.TryGet<int>() == 99);

    Variant c = Move(b);      // move
    CHECK(*c.TryGet<int>() == 99);
    CHECK(b.IsEmpty());

    a.Reset();
    CHECK(a.IsEmpty());
}

TEST_CASE("variant: manages non-trivial payload lifetimes")
{
    struct Tracked
    {
        static int& Live() { static int n = 0; return n; }
        int value;
        explicit Tracked(int v = 0) : value(v) { ++Live(); }
        Tracked(const Tracked& o) : value(o.value) { ++Live(); }
        Tracked(Tracked&& o) noexcept : value(o.value) { ++Live(); }
        ~Tracked() { --Live(); }
    };

    Tracked::Live() = 0;
    {
        Variant v = Variant::From(Tracked{ 5 });
        CHECK(Tracked::Live() == 1);
        Variant copy = v;
        CHECK(Tracked::Live() == 2);
        CHECK(copy.TryGet<Tracked>()->value == 5);
    }
    CHECK(Tracked::Live() == 0);
}

TEST_CASE("variant: Instance borrows without owning")
{
    int x = 17;
    Instance inst = Instance::From(&x);
    CHECK_FALSE(inst.IsEmpty());
    CHECK(inst.Type() == &TypeOf<int>());

    REQUIRE(inst.TryGet<int>() != nullptr);
    CHECK(*inst.TryGet<int>() == 17);
    *inst.TryGet<int>() = 23;
    CHECK(x == 23); // writes through to the borrowed object

    CHECK(inst.TryGet<float>() == nullptr); // wrong type
}

// --- RTTI: properties ------------------------------------------------------

TEST_CASE("rtti: reflected property is discoverable")
{
    CHECK(Properties(Animal::StaticType()).Size() == 1u);

    const PropertyInfo* legs = FindProperty(Animal::StaticType(), "legs");
    REQUIRE(legs != nullptr);
    CHECK(std::strcmp(legs->name, "legs") == 0);
    CHECK(legs->type == &TypeOf<int>());

    CHECK(FindProperty(Animal::StaticType(), "missing") == nullptr);
}

TEST_CASE("rtti: property get/set through an Instance")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator());
    const PropertyInfo* legs = FindProperty(Animal::StaticType(), "legs");
    REQUIRE(legs != nullptr);

    Instance inst = Instance::From(animal.Get());

    Variant got = GetProperty(*legs, inst);
    REQUIRE(got.Is<int>());
    CHECK(got.Get<int>() == 4); // default

    CHECK(SetProperty(*legs, inst, Variant::From(6)).IsOk());
    CHECK(animal->legs == 6); // mutated the real object
    CHECK(GetProperty(*legs, inst).Get<int>() == 6);

    // Wrong-typed value -> error, object unchanged.
    Status bad = SetProperty(*legs, inst, Variant::From(3.5f));
    CHECK_FALSE(bad.IsOk());
    CHECK(bad.Code() == ErrorCode::InvalidArgument);
    CHECK(animal->legs == 6);
}

TEST_CASE("rtti: inherited property is found through the base chain")
{
    // Dog declares no properties of its own but inherits 'legs' from Animal.
    CHECK(Properties(Dog::StaticType()).Size() == 0u);

    const PropertyInfo* legs = FindProperty(Dog::StaticType(), "legs");
    REQUIRE(legs != nullptr);

    RefPtr<Dog> dog = MakeRef<Dog>(DefaultAllocator());
    Instance inst = Instance::From(dog.Get());
    CHECK(SetProperty(*legs, inst, Variant::From(3)).IsOk());
    CHECK(dog->legs == 3);
}

// --- RTTI: methods ---------------------------------------------------------

TEST_CASE("rtti: instance method invoke with an argument and a return value")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator()); // legs = 4
    Instance inst = Instance::From(animal.Get());

    const MethodInfo* add = FindMethod(Animal::StaticType(), "AddLegs");
    REQUIRE(add != nullptr);
    CHECK_FALSE(add->isStatic);
    CHECK_FALSE(add->isConst);
    CHECK(add->returnType == &TypeOf<int>());
    CHECK(add->paramCount == 1u);
    CHECK(add->params[0].type == &TypeOf<int>());

    Variant args[] = { Variant::From(3) };
    Result<Variant> r = InvokeMethod(*add, inst, Span<Variant>{ args, 1 });
    REQUIRE(r.HasValue());
    CHECK(r.Value().Get<int>() == 7);
    CHECK(animal->legs == 7); // mutated the real object
}

TEST_CASE("rtti: const method and zero-arg invoke")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator());
    Instance inst = Instance::From(animal.Get());

    const MethodInfo* get = FindMethod(Animal::StaticType(), "GetLegs");
    REQUIRE(get != nullptr);
    CHECK(get->isConst);
    CHECK(get->paramCount == 0u);

    Result<Variant> r = InvokeMethod(*get, inst, Span<Variant>{});
    REQUIRE(r.HasValue());
    CHECK(r.Value().Get<int>() == 4);
}

TEST_CASE("rtti: static method invoke needs no instance")
{
    const MethodInfo* def = FindMethod(Animal::StaticType(), "DefaultLegs");
    REQUIRE(def != nullptr);
    CHECK(def->isStatic);

    Result<Variant> r = InvokeStatic(*def, Span<Variant>{});
    REQUIRE(r.HasValue());
    CHECK(r.Value().Get<int>() == 4);
}

TEST_CASE("rtti: method invoke rejects wrong arity and arg types")
{
    RefPtr<Animal> animal = MakeRef<Animal>(DefaultAllocator());
    Instance inst = Instance::From(animal.Get());
    const MethodInfo* add = FindMethod(Animal::StaticType(), "AddLegs");
    REQUIRE(add != nullptr);

    // Wrong arity.
    Result<Variant> noArgs = InvokeMethod(*add, inst, Span<Variant>{});
    CHECK_FALSE(noArgs.HasValue());
    CHECK(noArgs.Error() == ErrorCode::InvalidArgument);

    // Wrong argument type.
    Variant wrong[] = { Variant::From(2.5f) };
    Result<Variant> badType = InvokeMethod(*add, inst, Span<Variant>{ wrong, 1 });
    CHECK_FALSE(badType.HasValue());
    CHECK(badType.Error() == ErrorCode::InvalidArgument);

    CHECK(animal->legs == 4); // unchanged after failed calls
}

// --- RTTI: enums -----------------------------------------------------------

TEST_CASE("rtti: enum reflection exposes named values")
{
    RaptorRegisterEnum_Color();

    const TypeInfo& type = TypeOf<Color>();
    CHECK(IsEnum(type));
    CHECK(Enumerators(type).Size() == 3u);
    CHECK(std::strcmp(type.name, "Color") == 0);

    CHECK(std::strcmp(EnumValueName(type, static_cast<i64>(Color::Green)), "Green") == 0);
    CHECK(EnumValueName(type, 999) == nullptr);

    i64 value = 0;
    CHECK(EnumValueByName(type, "Blue", value));
    CHECK(value == static_cast<i64>(Color::Blue));
    CHECK_FALSE(EnumValueByName(type, "Purple", value));
}

TEST_CASE("rtti: enum values round-trip through a Variant")
{
    RaptorRegisterEnum_Color();

    Variant v = Variant::From(Color::Green);
    REQUIRE(v.Is<Color>());
    CHECK(v.Get<Color>() == Color::Green);
    CHECK(v.Type() == &TypeOf<Color>());
    CHECK(IsEnum(*v.Type()));
}

// --- RTTI: attributes ------------------------------------------------------

TEST_CASE("rtti: type attributes are queryable")
{
    const TypeInfo& type = Animal::StaticType();
    CHECK(Attributes(type).Size() == 2u);

    const Variant* scriptName = FindAttribute(type, "scriptName");
    REQUIRE(scriptName != nullptr);
    REQUIRE(scriptName->Is<const char*>());
    CHECK(std::strcmp(scriptName->Get<const char*>(), "Critter") == 0);

    const Variant* maxLegs = FindAttribute(type, "maxLegs");
    REQUIRE(maxLegs != nullptr);
    CHECK(maxLegs->Get<int>() == 8);

    CHECK(FindAttribute(type, "missing") == nullptr);
}

// --- IO: MemoryStream ------------------------------------------------------

TEST_CASE("io: MemoryStream write/read round-trip")
{
    MemoryStream stream;
    CHECK(stream.IsValid());
    CHECK(stream.Size() == 0);

    CHECK(stream.WriteValue<i32>(0x11223344));
    CHECK(stream.WriteValue<f32>(2.5f));
    const char text[] = "hi";
    CHECK(stream.Write(text, 2) == 2u);

    CHECK(stream.Size() == static_cast<i64>(sizeof(i32) + sizeof(f32) + 2));
    CHECK(stream.Tell() == stream.Size());

    // Rewind and read back.
    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);
    i32 a = 0;
    f32 b = 0.0f;
    char c[2] = {};
    CHECK(stream.ReadValue(a));
    CHECK(stream.ReadValue(b));
    CHECK(stream.Read(c, 2) == 2u);

    CHECK(a == 0x11223344);
    CHECK(b == 2.5f);
    CHECK(c[0] == 'h');
    CHECK(c[1] == 'i');

    // Reading past the end returns a short count.
    u8 extra = 0;
    CHECK(stream.Read(&extra, 1) == 0u);
}

TEST_CASE("io: MemoryStream seek bounds and overwrite")
{
    MemoryStream stream;
    CHECK(stream.WriteValue<i32>(10));
    CHECK(stream.WriteValue<i32>(20));

    // Seek out of range fails.
    CHECK(stream.Seek(-1, SeekOrigin::Begin) == -1);
    CHECK(stream.Seek(100, SeekOrigin::Begin) == -1);

    // Overwrite the first value in place.
    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);
    CHECK(stream.WriteValue<i32>(99));
    CHECK(stream.Size() == 8); // no growth

    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);
    i32 first = 0;
    i32 second = 0;
    CHECK(stream.ReadValue(first));
    CHECK(stream.ReadValue(second));
    CHECK(first == 99);
    CHECK(second == 20);
}

// --- IO: FileStream --------------------------------------------------------

TEST_CASE("io: FileStream writes then reads a file")
{
    const char* path = "raptor_io_stream_test.tmp";

    {
        FileStream out(path, FileMode::Write);
        REQUIRE(out.IsValid());
        CHECK(out.WriteValue<u32>(0xDEADBEEF));
        CHECK(out.WriteValue<f64>(3.25));
    }

    {
        FileStream in(path, FileMode::Read);
        REQUIRE(in.IsValid());
        CHECK(in.Size() == static_cast<i64>(sizeof(u32) + sizeof(f64)));

        u32 magic = 0;
        f64 value = 0.0;
        CHECK(in.ReadValue(magic));
        CHECK(in.ReadValue(value));
        CHECK(magic == 0xDEADBEEFu);
        CHECK(value == 3.25);

        // Seek back to the float and re-read.
        CHECK(in.Seek(static_cast<i64>(sizeof(u32)), SeekOrigin::Begin) == static_cast<i64>(sizeof(u32)));
        f64 again = 0.0;
        CHECK(in.ReadValue(again));
        CHECK(again == 3.25);
    }

    CHECK(FileDelete(path));
}

TEST_CASE("io: FileStream on an unopenable path is invalid")
{
    FileStream in("raptor_io_missing_file.xyz", FileMode::Read);
    CHECK_FALSE(in.IsValid());
    u8 byte = 0;
    CHECK(in.Read(&byte, 1) == 0u);
}

// --- Serialization ---------------------------------------------------------

namespace
{
    // A type that describes its data once, used for both save and load.
    struct Particle
    {
        i32 id = 0;
        Vec3 position;
        f32 mass = 0.0f;
    };

    void Serialize(ISerializer& ar, Particle& p)
    {
        Serialize(ar, p.id);
        Serialize(ar, p.position);
        Serialize(ar, p.mass);
    }
}

TEST_CASE("serialization: primitives and math round-trip")
{
    MemoryStream stream;
    {
        BinarySerializer saver(stream, SerializeDirection::Save);
        CHECK(saver.IsSaving());

        i32 a = -7;
        f64 b = 1.5;
        Vec3 v{ 1.0f, 2.0f, 3.0f };
        Serialize(saver, a);
        Serialize(saver, b);
        Serialize(saver, v);
        CHECK(saver.IsOk());
    }

    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer loader(stream, SerializeDirection::Load);
        CHECK(loader.IsLoading());

        i32 a = 0;
        f64 b = 0.0;
        Vec3 v;
        Serialize(loader, a);
        Serialize(loader, b);
        Serialize(loader, v);
        CHECK(loader.IsOk());

        CHECK(a == -7);
        CHECK(b == 1.5);
        CHECK(v == Vec3{ 1.0f, 2.0f, 3.0f });
    }
}

TEST_CASE("serialization: String and Array round-trip")
{
    MemoryStream stream;
    {
        BinarySerializer saver(stream, SerializeDirection::Save);
        String name = u"raptor";
        Array<i32> values;
        for (i32 i = 0; i < 5; ++i) { values.PushBack(i * 11); }
        Serialize(saver, name);
        Serialize(saver, values);
        CHECK(saver.IsOk());
    }

    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer loader(stream, SerializeDirection::Load);
        String name;
        Array<i32> values;
        Serialize(loader, name);
        Serialize(loader, values);
        CHECK(loader.IsOk());

        CHECK(name == u"raptor");
        REQUIRE(values.Size() == 5u);
        CHECK(values[0] == 0);
        CHECK(values[4] == 44);
    }
}

TEST_CASE("serialization: a user type serialized once for both directions")
{
    MemoryStream stream;

    Particle original;
    original.id = 99;
    original.position = Vec3{ 4.0f, 5.0f, 6.0f };
    original.mass = 2.25f;

    {
        BinarySerializer saver(stream, SerializeDirection::Save);
        Serialize(saver, original);
        CHECK(saver.IsOk());
    }

    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);

    Particle loaded;
    {
        BinarySerializer loader(stream, SerializeDirection::Load);
        Serialize(loader, loaded);
        CHECK(loader.IsOk());
    }

    CHECK(loaded.id == 99);
    CHECK(loaded.position == Vec3{ 4.0f, 5.0f, 6.0f });
    CHECK(loaded.mass == 2.25f);
}

TEST_CASE("serialization: nested Array<String> round-trips")
{
    MemoryStream stream;
    {
        BinarySerializer saver(stream, SerializeDirection::Save);
        Array<String> words;
        words.PushBack(String(u"alpha"));
        words.PushBack(String(u"beta"));
        words.PushBack(String(u"gamma"));
        Serialize(saver, words);
        CHECK(saver.IsOk());
    }

    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);
    {
        BinarySerializer loader(stream, SerializeDirection::Load);
        Array<String> words;
        Serialize(loader, words);
        CHECK(loader.IsOk());

        REQUIRE(words.Size() == 3u);
        CHECK(words[0] == u"alpha");
        CHECK(words[2] == u"gamma");
    }
}

TEST_CASE("serialization: short read on load is reported")
{
    MemoryStream stream;
    {
        BinarySerializer saver(stream, SerializeDirection::Save);
        i16 small = 7;
        Serialize(saver, small); // only 2 bytes written
    }
    CHECK(stream.Seek(0, SeekOrigin::Begin) == 0);

    BinarySerializer loader(stream, SerializeDirection::Load);
    i64 tooBig = 0; // wants 8 bytes
    Serialize(loader, tooBig);
    CHECK_FALSE(loader.IsOk()); // ran out of bytes
    CHECK(loader.GetStatus().Code() == ErrorCode::Internal);
}

// --- Threading -------------------------------------------------------------

TEST_CASE("threading: a thread runs and joins")
{
    Atomic<int> ran{ 0 };
    Thread t([&ran]() { ran.fetch_add(1); });
    CHECK(t.IsJoinable());
    t.Join();
    CHECK_FALSE(t.IsJoinable());
    CHECK(ran.load() == 1);
}

TEST_CASE("threading: Atomic fetch_add from many threads is exact")
{
    Atomic<i64> counter{ 0 };
    constexpr int kThreads = 8;
    constexpr int kPerThread = 10000;

    Array<Thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.PushBack(Thread([&counter]() {
            for (int j = 0; j < kPerThread; ++j) { counter.fetch_add(1); }
        }));
    }
    for (Thread& t : threads) { t.Join(); }

    CHECK(counter.load() == static_cast<i64>(kThreads) * kPerThread);
}

TEST_CASE("threading: Mutex/ScopedLock protects a non-atomic counter")
{
    Mutex mutex;
    i64 counter = 0;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 10000;

    Array<Thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.PushBack(Thread([&mutex, &counter]() {
            for (int j = 0; j < kPerThread; ++j)
            {
                ScopedLock lock(mutex);
                ++counter;
            }
        }));
    }
    for (Thread& t : threads) { t.Join(); }

    CHECK(counter == static_cast<i64>(kThreads) * kPerThread);
}

TEST_CASE("threading: ConditionVariable hand-off between two threads")
{
    Mutex mutex;
    ConditionVariable cv;
    bool ready = false;
    int payload = 0;

    Thread consumer([&]() {
        ScopedLock lock(mutex);
        while (!ready) { cv.Wait(mutex); }
        payload += 1; // observe the produced value
    });

    {
        ScopedLock lock(mutex);
        payload = 41;
        ready = true;
        cv.NotifyOne();
    }

    consumer.Join();
    CHECK(payload == 42);
}

// --- Library ---------------------------------------------------------------

TEST_CASE("library: load a real plugin, resolve and call symbols, unload")
{
    DynamicLibrary lib;
    CHECK_FALSE(lib.IsLoaded());

    REQUIRE(lib.Load(RAPTOR_TEST_PLUGIN_PATH).IsOk());
    CHECK(lib.IsLoaded());

    using AddFn = int (*)(int, int);
    AddFn add = lib.GetSymbol<AddFn>("RaptorTestAdd");
    REQUIRE(add != nullptr);
    CHECK(add(2, 3) == 5);

    using AnswerFn = int (*)();
    AnswerFn answer = lib.GetSymbol<AnswerFn>("RaptorTestAnswer");
    REQUIRE(answer != nullptr);
    CHECK(answer() == 42);

    CHECK(lib.GetSymbol<AddFn>("NoSuchSymbol") == nullptr);

    lib.Unload();
    CHECK_FALSE(lib.IsLoaded());
}

TEST_CASE("library: loading a missing file fails cleanly")
{
    DynamicLibrary lib;
    Status status = lib.Load("raptor_definitely_not_a_library.so");
    CHECK_FALSE(status.IsOk());
    CHECK(status.Code() == ErrorCode::NotFound);
    CHECK_FALSE(lib.IsLoaded());
}

TEST_CASE("library: move transfers ownership")
{
    DynamicLibrary a;
    REQUIRE(a.Load(RAPTOR_TEST_PLUGIN_PATH).IsOk());

    DynamicLibrary b = Move(a);
    CHECK_FALSE(a.IsLoaded());
    CHECK(b.IsLoaded());

    using AnswerFn = int (*)();
    AnswerFn answer = b.GetSymbol<AnswerFn>("RaptorTestAnswer");
    REQUIRE(answer != nullptr);
    CHECK(answer() == 42);
}
