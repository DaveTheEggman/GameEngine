// Draconic GUI - LinearLayout tests: children stacked in a row/column with spacing, re-run
// on add and on size/orientation/spacing changes, skipping hidden children.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }

    core::RefPtr<UIWidget> Child(core::Float2 size)
    {
        auto w = Make<UIWidget>();
        w->SetSize(size);
        return w;
    }
}

TEST_CASE("linear-layout: vertical stacks children on add")
{
    auto layout = Make<LinearLayout>();
    layout->SetSize(core::Float2{ 100.0f, 200.0f });
    layout->SetSpacing(5.0f);

    auto a = Child(core::Float2{ 80.0f, 20.0f });
    auto b = Child(core::Float2{ 80.0f, 30.0f });
    layout->AddChild(a.Get());
    layout->AddChild(b.Get());

    CHECK(a->GetPosition().y == doctest::Approx(0.0f));
    CHECK(b->GetPosition().y == doctest::Approx(25.0f)); // 20 + 5 spacing
    CHECK(a->GetPosition().x == doctest::Approx(0.0f));
    CHECK(b->GetPosition().x == doctest::Approx(0.0f));
}

TEST_CASE("linear-layout: horizontal stacks along x")
{
    auto layout = Make<LinearLayout>();
    layout->SetSize(core::Float2{ 300.0f, 50.0f });
    layout->SetOrientation(Orientation::Horizontal);
    layout->SetSpacing(10.0f);

    auto a = Child(core::Float2{ 40.0f, 40.0f });
    auto b = Child(core::Float2{ 60.0f, 40.0f });
    layout->AddChild(a.Get());
    layout->AddChild(b.Get());

    CHECK(a->GetPosition().x == doctest::Approx(0.0f));
    CHECK(b->GetPosition().x == doctest::Approx(50.0f)); // 40 + 10
}

TEST_CASE("linear-layout: padding offsets the start")
{
    auto layout = Make<LinearLayout>();
    layout->SetSize(core::Float2{ 100.0f, 200.0f });
    layout->SetPadding(Thickness{ 8.0f });

    auto a = Child(core::Float2{ 50.0f, 20.0f });
    layout->AddChild(a.Get());
    CHECK(a->GetPosition().x == doctest::Approx(8.0f));
    CHECK(a->GetPosition().y == doctest::Approx(8.0f));
}

TEST_CASE("linear-layout: hidden children are skipped")
{
    auto layout = Make<LinearLayout>();
    layout->SetSize(core::Float2{ 100.0f, 200.0f });

    auto a = Child(core::Float2{ 50.0f, 20.0f });
    auto b = Child(core::Float2{ 50.0f, 30.0f });
    auto c = Child(core::Float2{ 50.0f, 40.0f });
    layout->AddChild(a.Get());
    layout->AddChild(b.Get());
    layout->AddChild(c.Get());

    b->SetVisible(false);
    layout->PerformLayout(); // re-run after visibility change

    CHECK(a->GetPosition().y == doctest::Approx(0.0f));
    CHECK(c->GetPosition().y == doctest::Approx(20.0f)); // b (hidden) contributes no offset
}

TEST_CASE("linear-layout: relayouts on orientation change")
{
    auto layout = Make<LinearLayout>();
    layout->SetSize(core::Float2{ 200.0f, 200.0f });
    auto a = Child(core::Float2{ 30.0f, 30.0f });
    auto b = Child(core::Float2{ 30.0f, 30.0f });
    layout->AddChild(a.Get());
    layout->AddChild(b.Get());
    CHECK(b->GetPosition().y == doctest::Approx(30.0f)); // vertical default

    layout->SetOrientation(Orientation::Horizontal);
    CHECK(b->GetPosition().x == doctest::Approx(30.0f));
    CHECK(b->GetPosition().y == doctest::Approx(0.0f));
}
