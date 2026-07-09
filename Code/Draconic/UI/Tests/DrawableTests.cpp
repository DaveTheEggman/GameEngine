// Unit tests for the draconic.ui Drawing foundation: ControlState flags + Drawable/Cast<T>.
// Rendering itself is exercised by the UI sandbox; here we cover the non-rendering surface
// (state flags, our-RTTI downcasting, intrinsic size / padding defaults).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;

using namespace draconic::ui;
namespace core = draconic::core;

TEST_CASE("ui.control-state: bit flags combine + mask")
{
    ControlState s = ControlState::Hover | ControlState::Checked;
    CHECK(HasFlag(s, ControlState::Hover));
    CHECK(HasFlag(s, ControlState::Checked));
    CHECK_FALSE(HasFlag(s, ControlState::Pressed));
    CHECK(Any(s));
    CHECK_FALSE(Any(ControlState::Normal));

    s &= ~ControlState::Hover;
    CHECK_FALSE(HasFlag(s, ControlState::Hover));
    CHECK(HasFlag(s, ControlState::Checked));
}

TEST_CASE("ui.drawable: ColorDrawable via our RTTI (Cast<T>)")
{
    core::RefPtr<Drawable> d = core::MakeRef<ColorDrawable>(core::DefaultAllocator(), core::Color{ 1.0f, 0.0f, 0.0f, 1.0f });
    REQUIRE(d);

    // Concrete downcast through the base-chain Cast<T> (our -fno-rtti replacement for dynamic_cast).
    ColorDrawable* cd = core::Cast<ColorDrawable>(d.Get());
    REQUIRE(cd != nullptr);
    CHECK(cd->Color.r == 1.0f);
    CHECK(cd->Color.a == 1.0f);

    // Base defaults.
    CHECK_FALSE(d->IntrinsicSize().HasValue());
    CHECK(d->DrawablePadding().IsZero());

    // Type identity.
    CHECK(d->GetType() == &ColorDrawable::StaticType());
    CHECK(core::IsA<Drawable>(d.Get()));
}

TEST_CASE("ui.debug-settings: AnyEnabled")
{
    UIDebugDrawSettings s;
    CHECK_FALSE(s.AnyEnabled());
    s.ShowBounds = true;
    CHECK(s.AnyEnabled());
}
