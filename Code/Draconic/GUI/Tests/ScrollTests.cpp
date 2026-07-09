// Draconic GUI - ScrollBar + ScrollView tests: scroll offset clamping, wheel bubbling from
// hovered content, drag (with the leave-during-capture regression), and ScrollBar<->ScrollView
// composition.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.vg;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;
namespace vg = draconic::vg;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }

    core::RefPtr<UIWidget> Cell(float w, float h)
    {
        auto n = core::MakeRef<UIWidget>(core::DefaultAllocator());
        n->SetSize(core::Float2{ w, h });
        return n;
    }
}

TEST_CASE("scrollview: range and offset clamp to the content extent")
{
    auto sv = Make<ScrollView>();
    sv->SetSize(core::Float2{ 100.0f, 100.0f }); // viewport 100x100
    sv->SetContentSize(core::Float2{ 100.0f, 300.0f }); // vertical scroll only, range 200

    CHECK(sv->ScrollRange().x == doctest::Approx(0.0f));
    CHECK(sv->ScrollRange().y == doctest::Approx(200.0f));

    sv->SetScrollOffset(core::Float2{ 50.0f, 50.0f }); // x clamps to 0, y ok
    CHECK(sv->GetScrollOffset().x == doctest::Approx(0.0f));
    CHECK(sv->GetScrollOffset().y == doctest::Approx(50.0f));

    sv->SetScrollOffset(core::Float2{ 0.0f, 999.0f }); // clamps to range.y
    CHECK(sv->GetScrollOffset().y == doctest::Approx(200.0f));
}

TEST_CASE("scrollview: scrolling offsets the content container")
{
    auto sv = Make<ScrollView>();
    sv->SetSize(core::Float2{ 100.0f, 100.0f });
    sv->SetContentSize(core::Float2{ 100.0f, 300.0f });

    // Content starts at the viewport origin (no padding here).
    CHECK(sv->GetContent()->GetPosition().y == doctest::Approx(0.0f));
    sv->SetScrollOffset(core::Float2{ 0.0f, 40.0f });
    CHECK(sv->GetContent()->GetPosition().y == doctest::Approx(-40.0f)); // scrolled up
}

TEST_CASE("scrollview: content is clipped to the viewport")
{
    auto sv = Make<ScrollView>();
    sv->SetSize(core::Float2{ 100.0f, 100.0f });
    CHECK(sv->ClipsChildren());
}

TEST_CASE("scrollview: wheel bubbles from hovered content and scrolls")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 200.0f });
    auto sv = Make<ScrollView>();
    sv->SetSize(core::Float2{ 100.0f, 100.0f });
    sv->SetContentSize(core::Float2{ 100.0f, 300.0f });
    root->AddChild(sv.Get());

    // A child inside the content (so the wheel lands on it, not the ScrollView).
    auto item = Cell(100.0f, 40.0f);
    sv->GetContent()->AddChild(item.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    // Wheel down (negative y) over the item -> ScrollView scrolls down by wheelSpeed.
    sv->SetWheelSpeed(30.0f);
    d->InjectMouseWheel(core::Float2{ 20.0f, 20.0f }, core::Float2{ 0.0f, -1.0f });
    CHECK(sv->GetScrollOffset().y == doctest::Approx(30.0f));

    // Wheel up brings it back.
    d->InjectMouseWheel(core::Float2{ 20.0f, 20.0f }, core::Float2{ 0.0f, 1.0f });
    CHECK(sv->GetScrollOffset().y == doctest::Approx(0.0f));
}

TEST_CASE("scrollview: scroll fraction round-trips with SetVerticalFraction")
{
    auto sv = Make<ScrollView>();
    sv->SetSize(core::Float2{ 100.0f, 100.0f });
    sv->SetContentSize(core::Float2{ 100.0f, 300.0f }); // range 200

    sv->SetVerticalFraction(0.5f);
    CHECK(sv->GetScrollOffset().y == doctest::Approx(100.0f));
    CHECK(sv->GetScrollFraction().y == doctest::Approx(0.5f));
    sv->SetVerticalFraction(1.0f);
    CHECK(sv->GetScrollOffset().y == doctest::Approx(200.0f));
}

TEST_CASE("scrollbar: value clamps and notifies; thumb proportion clamps")
{
    auto sb = Make<ScrollBar>();
    int changes = 0; float last = -1.0f;
    sb->SetOnValueChanged([&](float v) { ++changes; last = v; });

    sb->SetValue(0.4f);
    CHECK(sb->GetValue() == doctest::Approx(0.4f));
    CHECK(changes == 1);
    CHECK(last == doctest::Approx(0.4f));
    sb->SetValue(5.0f);
    CHECK(sb->GetValue() == doctest::Approx(1.0f));

    sb->SetThumbProportion(2.0f);
    CHECK(sb->GetThumbProportion() == doctest::Approx(1.0f));
}

TEST_CASE("scrollbar: drag keeps tracking after the cursor leaves it (capture + hover leave)")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 400.0f });
    auto sb = Make<ScrollBar>();
    sb->SetOrientation(Orientation::Vertical);
    sb->SetSize(core::Float2{ 16.0f, 200.0f }); // vertical track length 200
    sb->SetThumbProportion(0.5f);               // thumb 100 -> travel 100
    root->AddChild(sb.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    // Hover first (so a leave will fire when we drag off), then press near the top.
    d->InjectMouseMove(core::Float2{ 8.0f, 10.0f });
    CHECK(sb->IsHovered());
    d->InjectMouseDown(core::Float2{ 8.0f, 10.0f }, MouseButton::Left);

    // Drag well past the bottom edge -> value saturates at 1 (still tracking after leave).
    d->InjectMouseMove(core::Float2{ 8.0f, 500.0f });
    CHECK_FALSE(sb->IsHovered());
    CHECK(sb->GetValue() == doctest::Approx(1.0f));

    // Release ends the drag.
    d->InjectMouseUp(core::Float2{ 8.0f, 500.0f }, MouseButton::Left);
    d->InjectMouseMove(core::Float2{ 8.0f, 10.0f });
    CHECK(sb->GetValue() == doctest::Approx(1.0f));
}

TEST_CASE("scrollbar + scrollview compose: bar value drives the view")
{
    auto sv = Make<ScrollView>();
    sv->SetSize(core::Float2{ 100.0f, 100.0f });
    sv->SetContentSize(core::Float2{ 100.0f, 300.0f }); // range 200

    auto sb = Make<ScrollBar>();
    ScrollView* view = sv.Get();
    sb->SetOnValueChanged([view](float v) { view->SetVerticalFraction(v); });

    sb->SetValue(0.25f);
    CHECK(sv->GetScrollOffset().y == doctest::Approx(50.0f)); // 0.25 * 200
    sb->SetValue(1.0f);
    CHECK(sv->GetScrollOffset().y == doctest::Approx(200.0f));
}

TEST_CASE("scrollbar: draws track + thumb")
{
    auto sb = Make<ScrollBar>();
    sb->SetSize(core::Float2{ 16.0f, 120.0f });
    sb->SetValue(0.5f);

    vg::VGContext ctx;
    DrawContext dc{ ctx };
    sb->Draw(dc);
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}
