#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

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
