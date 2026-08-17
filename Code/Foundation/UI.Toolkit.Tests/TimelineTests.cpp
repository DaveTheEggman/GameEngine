// Timeline widget tests (property-animation-editor.md A8, widget slice - headless). The Timeline is
// domain-agnostic (no clip), so these exercise the pure surface: the shared time<->pixel transform
// (D1) round-trips through zoom + scroll, the ruler tick step follows {1,2,5}x10^n and never lets
// labels collide, and the playhead clamps + fires OnPlayheadMoved only on a real change.

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::core;
using namespace foundation::ui::toolkit;

TEST_CASE("timeline: PickTickStep follows {1,2,5}x10^n and never collides labels")
{
    // 1s = 100px, min label 48px -> min step 0.48s -> 0.5s (5 x 0.1).
    CHECK(Timeline::PickTickStep(100.0f, 48.0f) == doctest::Approx(0.5f));
    // 1s = 20px -> min 2.4s -> 5s.
    CHECK(Timeline::PickTickStep(20.0f, 48.0f) == doctest::Approx(5.0f));
    // 1s = 1000px -> min 0.048s -> 0.05s (5 x 0.01).
    CHECK(Timeline::PickTickStep(1000.0f, 48.0f) == doctest::Approx(0.05f));
    // 1s = 10px -> min 4.8s -> 5s.
    CHECK(Timeline::PickTickStep(10.0f, 48.0f) == doctest::Approx(5.0f));
    // 1s = 5px -> min 9.6s -> 10s (rolls to the next decade).
    CHECK(Timeline::PickTickStep(5.0f, 48.0f) == doctest::Approx(10.0f));

    // The chosen step always spans at least the minimum label width on screen (no collisions).
    const f32 zooms[] = {5.0f, 10.0f, 37.0f, 100.0f, 250.0f, 1000.0f, 3999.0f};
    for (const f32 pps : zooms)
    {
        const f32 step = Timeline::PickTickStep(pps, 48.0f);
        CHECK(step * pps >= 48.0f);
    }
    // Degenerate inputs never divide-by-zero.
    CHECK(Timeline::PickTickStep(0.0f, 48.0f) == doctest::Approx(1.0f));
}

TEST_CASE("timeline: time<->pixel transform round-trips through zoom + scroll (D1)")
{
    auto tl = MakeRef<Timeline>(DefaultAllocator());
    tl->SetPixelsPerSecond(120.0f);
    tl->SetScrollSeconds(0.5f);
    tl->LabelColumnWidth = 40.0f;

    // x = label + (t - scroll) * pps.
    CHECK(tl->TimeToX(0.5f) == doctest::Approx(40.0f));
    CHECK(tl->TimeToX(1.5f) == doctest::Approx(160.0f));

    const f32 times[] = {0.0f, 0.5f, 1.2f, 3.7f};
    for (const f32 t : times)
    {
        CHECK(tl->XToTime(tl->TimeToX(t)) == doctest::Approx(t));
    }

    // Zoom is clamped (no runaway); the transform stays consistent after a clamp.
    tl->SetPixelsPerSecond(1.0f); // below the min -> clamped
    CHECK(tl->PixelsPerSecond() >= 4.0f);
    CHECK(tl->XToTime(tl->TimeToX(2.0f)) == doctest::Approx(2.0f));
}

TEST_CASE("timeline: playhead clamps to [0,duration] and fires only on change")
{
    auto tl = MakeRef<Timeline>(DefaultAllocator());
    tl->SetDuration(2.0f);

    i32 fires = 0;
    f32 last = -1.0f;
    tl->OnPlayheadMoved.Add(
        [&](f32 t)
        {
            ++fires;
            last = t;
        });

    tl->SetPlayheadTime(1.0f);
    CHECK(tl->PlayheadTime() == doctest::Approx(1.0f));
    CHECK(fires == 1);
    CHECK(last == doctest::Approx(1.0f));

    tl->SetPlayheadTime(1.0f); // no change -> no event
    CHECK(fires == 1);

    tl->SetPlayheadTime(5.0f); // clamp to duration
    CHECK(tl->PlayheadTime() == doctest::Approx(2.0f));
    CHECK(fires == 2);

    tl->SetPlayheadTime(-3.0f); // clamp to 0
    CHECK(tl->PlayheadTime() == doctest::Approx(0.0f));
    CHECK(fires == 3);

    // Shrinking the duration re-clamps the playhead.
    tl->SetPlayheadTime(2.0f);
    tl->SetDuration(1.0f);
    CHECK(tl->PlayheadTime() == doctest::Approx(1.0f));
}
