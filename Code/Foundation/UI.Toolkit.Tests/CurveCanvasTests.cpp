// Smoke test for the toolkit CurveCanvas: set channels, set keys, read them back, check defaults.
// No font/VG rendering, no input simulation (events fire only from mouse handlers).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::ui;
using namespace foundation::ui::toolkit;
using namespace foundation::core;
namespace core = foundation::core;

TEST_CASE("toolkit-curvecanvas: SetChannelsAndKeys")
{
    auto cv = core::MakeRef<CurveCanvas>(core::DefaultAllocator());

    // Defaults on a fresh canvas.
    CHECK(cv->ChannelCount() == 0);
    CHECK(cv->SelectedChannel() == -1);
    CHECK(cv->SelectedKeyIndex() == -1);
    CHECK(cv->MaxKeys == 8);
    CHECK(cv->LinkedTime == false);
    CHECK(cv->AutoFitValueRange == true);

    // Configure two channels.
    ChannelDescriptor descs[2];
    descs[0].Name = String(u8"X");
    descs[0].StrokeColor = core::Color{1, 0, 0, 1};
    descs[0].Interpolation = CurveInterpolation::Hermite;
    descs[1].Name = String(u8"Y");
    descs[1].StrokeColor = core::Color{0, 1, 0, 1};
    descs[1].Interpolation = CurveInterpolation::Linear;

    cv->SetChannels(Span<const ChannelDescriptor>(descs, 2));
    CHECK(cv->ChannelCount() == 2);
    // Selection resets to channel 0 when channels exist.
    CHECK(cv->SelectedChannel() == 0);
    CHECK(cv->SelectedKeyIndex() == -1);

    // Descriptor round-trips.
    CHECK(cv->GetChannelDescriptor(1).Interpolation == CurveInterpolation::Linear);

    // Channels start with no keys.
    CHECK(cv->GetKeyCount(0) == 0);
    CHECK(cv->GetKeyCount(1) == 0);
}

TEST_CASE("toolkit-curvecanvas: SetKeysRoundTrip")
{
    auto cv = core::MakeRef<CurveCanvas>(core::DefaultAllocator());

    ChannelDescriptor descs[1];
    descs[0].Name = String(u8"V");
    cv->SetChannels(Span<const ChannelDescriptor>(descs, 1));

    CurveCanvas::Key keys[3] = {
        CurveCanvas::Key{0.0f, 0.0f},
        CurveCanvas::Key{0.5f, 1.0f, 0.25f, -0.25f, TangentMode::Free},
        CurveCanvas::Key{1.0f, 0.0f},
    };
    cv->SetKeys(0, Span<const CurveCanvas::Key>(keys, 3));

    CHECK(cv->GetKeyCount(0) == 3);

    const CurveCanvas::Key k1 = cv->GetKey(0, 1);
    CHECK(k1.Time == doctest::Approx(0.5f));
    CHECK(k1.Value == doctest::Approx(1.0f));
    CHECK(k1.TangentIn == doctest::Approx(0.25f));
    CHECK(k1.TangentOut == doctest::Approx(-0.25f));
    CHECK(k1.Mode == TangentMode::Free);

    // Out-of-range channel index is a no-op (does not crash / mutate).
    cv->SetKeys(5, Span<const CurveCanvas::Key>(keys, 3));
    CHECK(cv->GetKeyCount(0) == 3);
}

TEST_CASE("toolkit-curvecanvas: ValueRangeDefaults")
{
    auto cv = core::MakeRef<CurveCanvas>(core::DefaultAllocator());
    CHECK(cv->ValueMin == doctest::Approx(0.0f));
    CHECK(cv->ValueMax == doctest::Approx(1.0f));

    // Explicit value range is honored when auto-fit is disabled.
    cv->AutoFitValueRange = false;
    cv->ValueMin = -2.0f;
    cv->ValueMax = 3.0f;
    CHECK(cv->ValueMin == doctest::Approx(-2.0f));
    CHECK(cv->ValueMax == doctest::Approx(3.0f));
}

TEST_CASE("toolkit-curvecanvas: clicking ANY key fires OnSelectionChanged with its indices")
{
    // The regression: only the value readout driven by scrub-time samples LOOKED selection-
    // driven; there was no selection event at all. Every key pick must now report itself.
    auto cv = core::MakeRef<CurveCanvas>(core::DefaultAllocator());
    ChannelDescriptor d;
    d.Name = String(u8"X");
    cv->SetChannels(Span<const ChannelDescriptor>(&d, 1));
    cv->TimeSpan = 4.0f;
    // Pin the value range: hit-testing auto-fits (with a margin) BEFORE testing, so computing
    // click positions from the pre-fit defaults would miss and click-ADD instead.
    cv->AutoFitValueRange = false;
    cv->ValueMin = 0.0f;
    cv->ValueMax = 1.0f;
    CurveCanvas::Key keys[3] = {CurveCanvas::Key{0.0f, 0.0f}, CurveCanvas::Key{2.0f, 0.5f},
                                CurveCanvas::Key{4.0f, 1.0f}};
    cv->SetKeys(0, Span<const CurveCanvas::Key>(keys, 3));
    cv->Measure(BoxConstraints::Tight(400.0f, 100.0f));
    cv->Layout(0.0f, 0.0f, 400.0f, 100.0f);

    i32 selCh = -2;
    i32 selKey = -2;
    i32 fires = 0;
    cv->OnSelectionChanged.Add(
        [&](i32 ch, i32 ki)
        {
            selCh = ch;
            selKey = ki;
            ++fires;
        });

    // TimeSpan 4 over 400 px -> t=2 s sits at x=200. Click each key in turn; every one
    // (not just the first) must report selection.
    const f32 keyXs[3] = {0.0f, 200.0f, 400.0f};
    for (i32 i = 0; i < 3; ++i)
    {
        foundation::ui::MouseEventArgs down;
        down.Button = foundation::ui::MouseButton::Left;
        down.X = keyXs[i];
        // AutoFit frames the value range with a margin; hit the key's exact screen y.
        down.Y = 100.0f * (1.0f - (keys[i].Value - cv->ValueMin) /
                                      (cv->ValueMax - cv->ValueMin));
        cv->OnMouseDown(down);
        foundation::ui::MouseEventArgs up = down;
        cv->OnMouseUp(up);
        CHECK(selCh == 0);
        CHECK(selKey == i);
    }
    CHECK(fires == 3); // one per pick, none skipped, no duplicate for re-clicking
}
