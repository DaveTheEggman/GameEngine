// Draconic GUI - Slider tests: value clamping + change callback, drag-to-set, and pointer
// capture (a drag keeps tracking after the cursor leaves the slider).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.vg;
import draconic.gui;

using namespace draconic::gui;
namespace core = draconic::core;

namespace
{
    template <typename T> core::RefPtr<T> Make() { return core::MakeRef<T>(core::DefaultAllocator()); }
}

TEST_CASE("slider: value clamps and notifies on change")
{
    auto s = Make<Slider>();
    int changes = 0;
    float last = -1.0f;
    s->SetOnValueChanged([&](float v) { ++changes; last = v; });

    s->SetValue(0.5f);
    CHECK(s->GetValue() == doctest::Approx(0.5f));
    CHECK(changes == 1);
    CHECK(last == doctest::Approx(0.5f));

    s->SetValue(2.0f); // clamps to 1
    CHECK(s->GetValue() == doctest::Approx(1.0f));
    s->SetValue(-1.0f); // clamps to 0
    CHECK(s->GetValue() == doctest::Approx(0.0f));

    const int before = changes;
    s->SetValue(0.0f); // no change
    CHECK(changes == before);
}

TEST_CASE("slider: press sets value from the cursor x")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 200.0f, 200.0f });
    auto s = Make<Slider>();
    s->SetSize(core::Float2{ 100.0f, 20.0f }); // handleR = 10 -> usable x in [10, 90]
    root->AddChild(s.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    d->InjectMouseDown(core::Float2{ 50.0f, 10.0f }, MouseButton::Left); // (50-10)/(90-10) = 0.5
    CHECK(s->GetValue() == doctest::Approx(0.5f));
}

TEST_CASE("slider: drag keeps tracking after the cursor leaves it (pointer capture)")
{
    auto root = Make<SceneNode>();
    root->SetSize(core::Float2{ 300.0f, 200.0f });
    auto s = Make<Slider>();
    s->SetSize(core::Float2{ 100.0f, 20.0f });
    root->AddChild(s.Get());
    EventDispatcher* d = root->GetEventDispatcher();

    d->InjectMouseDown(core::Float2{ 10.0f, 10.0f }, MouseButton::Left); // value 0
    CHECK(s->GetValue() == doctest::Approx(0.0f));

    // Move the cursor well past the right edge of the slider - still captured, value saturates at 1.
    d->InjectMouseMove(core::Float2{ 250.0f, 10.0f });
    CHECK(s->GetValue() == doctest::Approx(1.0f));

    // Drag back to the middle.
    d->InjectMouseMove(core::Float2{ 50.0f, 10.0f });
    CHECK(s->GetValue() == doctest::Approx(0.5f));

    // Release ends the drag; a later move (button up) no longer changes the value.
    d->InjectMouseUp(core::Float2{ 50.0f, 10.0f }, MouseButton::Left);
    d->InjectMouseMove(core::Float2{ 90.0f, 10.0f });
    CHECK(s->GetValue() == doctest::Approx(0.5f));
}

TEST_CASE("slider: draws track + fill + handle")
{
    auto s = Make<Slider>();
    s->SetSize(core::Float2{ 120.0f, 20.0f });
    s->SetValue(0.5f);

    draconic::vg::VGContext ctx;
    DrawContext dc{ ctx };
    s->Draw(dc);
    CHECK(ctx.GetBatch().vertices.Size() > 0);
}
