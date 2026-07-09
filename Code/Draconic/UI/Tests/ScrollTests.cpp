// Direct unit coverage for MomentumHelper + ScrollBar (Sedulous has no standalone test files for these;
// it exercises them through ScrollView, which is ported separately in ScrollViewTests). Logic only - no
// font service needed.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import draconic.core;
import draconic.ui;
#include "TestHelpers.h"

using namespace draconic::ui;
using namespace draconic::ui::tests;
using namespace draconic::core;
namespace core = draconic::core;

// === MomentumHelper ===

TEST_CASE("momentum: IsActive tracks the stop threshold")
{
    MomentumHelper m;
    CHECK(!m.IsActive());               // zero velocity
    m.VelocityY = 0.1f;                 // below StopThreshold (0.5)
    CHECK(!m.IsActive());
    m.VelocityY = 10.0f;
    CHECK(m.IsActive());
}

TEST_CASE("momentum: Update returns displacement and decays velocity")
{
    MomentumHelper m; m.VelocityY = 100.0f; m.Friction = 6.0f;
    const Float2 d = m.Update(0.016f);
    CHECK(d.y == doctest::Approx(1.6f));      // dy = velocity * dt
    CHECK(d.x == doctest::Approx(0.0f));
    CHECK(m.VelocityY < 100.0f);              // decayed
    CHECK(m.VelocityY == doctest::Approx(100.0f * (1.0f - 6.0f * 0.016f)));
}

TEST_CASE("momentum: low velocity snaps to zero; inactive Update is a no-op")
{
    MomentumHelper m; m.VelocityX = 0.3f; // below threshold -> inactive
    const Float2 d = m.Update(0.016f);
    CHECK(d.x == doctest::Approx(0.0f));
    CHECK(d.y == doctest::Approx(0.0f));

    MomentumHelper m2; m2.VelocityX = 100.0f;
    for (int i = 0; i < 200; ++i) { m2.Update(0.016f); }
    CHECK(!m2.IsActive());
    CHECK(m2.VelocityX == 0.0f);
}

TEST_CASE("momentum: Stop zeroes velocity")
{
    MomentumHelper m; m.VelocityX = 50; m.VelocityY = -30;
    m.Stop();
    CHECK(m.VelocityX == 0.0f);
    CHECK(m.VelocityY == 0.0f);
    CHECK(!m.IsActive());
}

// === ScrollBar ===

static core::RefPtr<ScrollBar> MakeBar(bool horizontal = false) { return core::MakeRef<ScrollBar>(core::DefaultAllocator(), horizontal); }

TEST_CASE("scroll-bar: Value clamps to [0, MaxValue]")
{
    auto sb = MakeBar();
    sb->SetMaxValue(100);
    sb->SetValue(150); CHECK(sb->Value() == 100);
    sb->SetValue(-10); CHECK(sb->Value() == 0);
    sb->SetValue(50);  CHECK(sb->Value() == 50);
}

TEST_CASE("scroll-bar: SetMaxValue reclamps current value; ViewportSize floors at 1")
{
    auto sb = MakeBar();
    sb->SetMaxValue(100); sb->SetValue(80);
    sb->SetMaxValue(50);  CHECK(sb->Value() == 50);   // clamped down
    CHECK(sb->MaxValue() == 50);

    sb->SetViewportSize(-5); CHECK(sb->ViewportSize() == 1); // floored
}

TEST_CASE("scroll-bar: OnValueChanged fires only on change")
{
    auto sb = MakeBar();
    sb->SetMaxValue(100);
    int fires = 0; f32 last = -1;
    sb->OnValueChanged.Add(Event<void(ScrollBar*, f32)>::Handler{ [&fires, &last](ScrollBar*, f32 v) { fires++; last = v; } });
    sb->SetValue(30); CHECK(fires == 1); CHECK(last == 30);
    sb->SetValue(30); CHECK(fires == 1); // unchanged -> no fire
    sb->SetValue(60); CHECK(fires == 2); CHECK(last == 60);
}

TEST_CASE("scroll-bar: GetThumbRect size + position track value (vertical)")
{
    auto sb = MakeBar(false);
    sb->SetMaxValue(100); sb->SetViewportSize(50);
    sb->Layout(0, 0, 12, 200); // width, height

    // ThumbRatio = 50/(100+50) = 1/3 -> thumb height ~66.67, width = bar width.
    sb->SetValue(0);
    const Rectangle top = sb->GetThumbRect();
    CHECK(top.width == doctest::Approx(12.0f));
    CHECK(top.height == doctest::Approx(200.0f / 3.0f));
    CHECK(top.y == doctest::Approx(0.0f));

    sb->SetValue(100);
    const Rectangle bottom = sb->GetThumbRect();
    CHECK(bottom.y == doctest::Approx(200.0f - 200.0f / 3.0f)); // fully scrolled
    CHECK(bottom.y > top.y);
}
