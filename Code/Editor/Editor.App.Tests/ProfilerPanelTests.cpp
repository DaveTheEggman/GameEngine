// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The editor's Profiler tab (Specs/profiler.md, P5), on a test's own store: Record arms it and
// copies settled frames into the panel's recording, following the newest until a frame is chosen;
// Stop lets the store go and keeps the frames; another viewer's arm and the store's history are
// left alone; Save writes a capture that Open shows, and Live goes back.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.profiler;
import foundation.profiler.capture;
import foundation.profiler.ui;
import foundation.ui;
import editor.app;

using namespace foundation::core;
using namespace foundation::profiler;
using namespace editor::app;
namespace pui = foundation::profiler::ui;

namespace
{
    void RunFrames(Profiler& p, u32 count)
    {
        for (u32 f = 0; f < count; ++f)
        {
            p.BeginFrame();
            if (p.BeginScope("Editor.Frame"))
            {
                p.EndScope();
            }
            p.EndFrame();
        }
    }

    RefPtr<ProfilerPanel> MakePanel(Profiler& store, CaptureRecorder& recorder, StringView folder)
    {
        ProfilerPanelSeams seams;
        seams.store = &store;
        seams.recorder = &recorder;
        seams.folder = [path = String(folder)]() { return path; };
        return MakeRef<ProfilerPanel>(DefaultAllocator(), Move(seams));
    }
}

TEST_CASE("profiler panel: Record copies settled frames and follows the newest; Stop keeps them")
{
    Profiler store(DefaultAllocator());
    CaptureRecorder recorder(DefaultAllocator());
    RefPtr<ProfilerPanel> panel = MakePanel(store, recorder, u8"scratch_profiler_panel");
    CHECK_FALSE(panel->IsRecording());
    CHECK_FALSE(store.Armed());
    CHECK(panel->StatusText().ContainsIgnoreCase(u8"Play-in-editor")); // it says what it shows: the whole process

    store.Arm(); // another viewer (a PIE game's overlay) already watching
    RunFrames(store, 3);
    panel->Start();
    CHECK(panel->IsRecording());
    RunFrames(store, 10); // frames 3..12
    panel->Update();
    CHECK(panel->Recording().OldestFrame() == 3u);
    CHECK(panel->Recording().NewestFrame() == 12u - ProfileRecording::kSettleFrames);
    // Following: the newest settled frame is the selection, in every view.
    CHECK(panel->Selection().first == panel->Recording().NewestFrame());
    CHECK(panel->Table().Rows().Size() == 1);
    CHECK(panel->StatusText().ContainsIgnoreCase(u8"Recording"));

    // A chosen frame stops the following.
    pui::FrameRange chosen;
    chosen.first = 4;
    chosen.last = 5;
    chosen.empty = false;
    panel->Select(chosen);
    RunFrames(store, 3);
    panel->Update();
    CHECK(panel->Selection().first == 4u);
    CHECK(panel->Selection().last == 5u);

    panel->Stop();
    CHECK_FALSE(panel->IsRecording());
    CHECK(panel->Recording().NewestFrame() == 15u); // the rest taken as they were
    CHECK(store.Armed());                           // the other viewer's arm is still held
    store.Disarm();
    CHECK_FALSE(store.Armed());
    CHECK(store.OldestKeptFrame() == 0u); // the store's history untouched
    RunFrames(store, 2);
    panel->Update(); // stopped: nothing more
    CHECK(panel->Recording().NewestFrame() == 15u);
}

TEST_CASE("profiler panel: Save writes a capture; Open shows it; Live goes back")
{
    Profiler store(DefaultAllocator());
    CaptureRecorder recorder(DefaultAllocator());
    recorder.SetBuild(StringView(u8"Linux64"), StringView(u8"test-build"));
    RefPtr<ProfilerPanel> panel = MakePanel(store, recorder, u8"scratch_profiler_panel");
    CHECK(panel->SaveRecording().IsEmpty()); // nothing recorded yet

    panel->Start();
    RunFrames(store, 8);
    panel->Stop();
    const String path = panel->SaveRecording();
    REQUIRE_FALSE(path.IsEmpty());
    CHECK(path.AsView().EndsWith(u8".aeprofile"));
    CHECK(path.AsView().ContainsIgnoreCase(u8"scratch_profiler_panel/Editor-"));
    CHECK(panel->StatusText().ContainsIgnoreCase(u8"Saved 8 frames"));

    REQUIRE(panel->OpenCapture(path.AsView()));
    CHECK(panel->ViewingCapture());
    REQUIRE(panel->Source() != nullptr);
    CHECK(panel->Source() != &panel->Recording());
    CHECK(panel->Source()->NewestFrame() == panel->Recording().NewestFrame());
    CHECK(panel->Selection().first == panel->Source()->NewestFrame()); // the newest frame shown
    CHECK(panel->StatusText().ContainsIgnoreCase(u8"test-build"));
    CHECK(panel->SaveRecording().IsEmpty()); // a capture is already a file

    panel->ShowLive();
    CHECK_FALSE(panel->ViewingCapture());
    CHECK(panel->Source() == &panel->Recording());

    CHECK_FALSE(panel->OpenCapture(u8"scratch_profiler_panel/not-there.aeprofile"));
    CHECK(panel->StatusText().ContainsIgnoreCase(u8"Could not read"));
    CHECK_FALSE(panel->ViewingCapture());
}

TEST_CASE("profiler panel: recording while closing lets the store go")
{
    Profiler store(DefaultAllocator());
    CaptureRecorder recorder(DefaultAllocator());
    {
        RefPtr<ProfilerPanel> panel = MakePanel(store, recorder, u8"scratch_profiler_panel");
        panel->Start();
        CHECK(store.Armed());
    }
    CHECK_FALSE(store.Armed());
}

TEST_CASE("profiler panel: its parts keep their sizes; a smaller pane scrolls, a larger one fills")
{
    Profiler store(DefaultAllocator());
    CaptureRecorder recorder(DefaultAllocator());
    RefPtr<ProfilerPanel> panel = MakePanel(store, recorder, u8"scratch_profiler_panel");
    foundation::ui::UIContext context{DefaultAllocator()};
    auto root = MakeRef<foundation::ui::RootView>(DefaultAllocator());
    context.AddRootView(root.Get());
    root->AddView(panel.Get());
    auto layOut = [&](f32 w, f32 h)
    {
        root->ViewportSize = Float2{w, h};
        context.BeginFrame(0.016f);
        root->Measure(foundation::ui::BoxConstraints::Tight(w, h));
        root->Layout(0, 0, w, h);
    };

    // The Console's pane in the default layout, about 560 by 180: the body keeps its minimum and
    // the pane scrolls it.
    layOut(560, 180);
    CHECK(panel->Body().Width() == doctest::Approx(f32{ProfilerPanelBody::kMinWidth}));
    CHECK(panel->Body().Height() == doctest::Approx(f32{ProfilerPanelBody::kMinHeight}));
    CHECK(panel->Strip().Height() == doctest::Approx(f32{pui::FrameTimeGraph::kHeight}));
    CHECK(panel->Scroller().Width() == doctest::Approx(560.0f));
    CHECK(panel->Scroller().Height() == doctest::Approx(180.0f));
    const f32 tableTop = panel->Table().Bounds.y;
    CHECK(panel->Counters().Bounds.y >= tableTop + panel->Table().Height());
    CHECK(panel->Counters().Bounds.y + panel->Counters().Height() <= ProfilerPanelBody::kMinHeight + 0.5f);

    // A pane larger than the minimum: the body fills it, the timeline and the table taking the room.
    layOut(1600, 600);
    CHECK(panel->Body().Width() == doctest::Approx(1600.0f));
    CHECK(panel->Body().Height() == doctest::Approx(600.0f));
    CHECK(panel->Table().Width() == doctest::Approx(f32{ProfilerPanelBody::kSideMax}));
    CHECK(panel->TimelineView().Width() == doctest::Approx(1600.0f - ProfilerPanelBody::kSideMax - ProfilerPanelBody::kGap));
    CHECK(ProfilerPanelBody::SideWidth(640.0f) == doctest::Approx(f32{ProfilerPanelBody::kSideMin}));
    context.RemoveRootView(root.Get());
}
