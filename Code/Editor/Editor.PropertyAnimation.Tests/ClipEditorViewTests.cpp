// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// ClipEditorView seam tests. The shared editing view lives
// over the IClipEditorHost seam, so it drives edits through a HOST's clip + command stack rather than
// owning them. These tests host it against a minimal fake (a clip + a real EditorCommandStack) and
// verify: the view builds headlessly over a clip, discrete edits route through the host's undo stack
// (add-track undo/redo round-trips the clip), and both render paths (scalar CurveCanvas + quaternion
// key table) build without a window. The standalone page and the in-scene
// tool panel both host the same view, so its behavior must be host-agnostic.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.propertyanimation;
import editor.core;
import editor.propertyanimation;

using namespace foundation::core;
using namespace editor;

namespace propanim = foundation::propertyanimation;

namespace
{
    // A minimal host: a clip, a real command stack, and a dirty counter. No page, no viewport.
    class FakeClipHost final : public IClipEditorHost
    {
    public:
        [[nodiscard]] propanim::PropertyAnimationClip& Clip() override { return clip; }
        [[nodiscard]] EditorCommandStack& Commands() override { return commands; }
        void MarkClipDirty() override { ++dirtyCount; }
        void OnScrubTimeChanged(f32 t) override { lastScrub = t; }
        void OnClipViewRebuilt() override { ++rebuildCount; }

        propanim::PropertyAnimationClip clip;
        EditorCommandStack commands;
        int dirtyCount = 0;
        int rebuildCount = 0;
        f32 lastScrub = -1.0f;
    };
}

TEST_CASE("clip-editor-view: builds headlessly and rebuilds on an empty clip")
{
    FakeClipHost host;
    ClipEditorView view(DefaultAllocator(), host);
    CHECK(view.Root() != nullptr);
    CHECK(host.rebuildCount >= 1); // the constructor's first Rebuild fired OnClipViewRebuilt
    const int before = host.rebuildCount;
    view.Rebuild(); // idempotent, no crash
    CHECK(host.rebuildCount == before + 1);
}

TEST_CASE("clip-editor-view: add-track routes through the host command stack (undo/redo)")
{
    FakeClipHost host;
    ClipEditorView view(DefaultAllocator(), host);
    CHECK(host.clip.tracks.Size() == 0);

    view.AddTrack(u8"Transform", u8"position", propanim::TrackValueKind::Float3);
    CHECK(host.clip.tracks.Size() == 1);
    CHECK(host.clip.tracks[0].componentType.AsView() == StringView(u8"Transform"));
    CHECK(host.clip.tracks[0].propertyPath.AsView() == StringView(u8"position"));
    CHECK(host.clip.tracks[0].kind == propanim::TrackValueKind::Float3);
    CHECK(host.commands.CanUndo());

    host.commands.Undo();
    CHECK(host.clip.tracks.Size() == 0);
    CHECK(host.commands.CanRedo());

    host.commands.Redo();
    CHECK(host.clip.tracks.Size() == 1);
}

TEST_CASE("clip-editor-view: both track render paths build (scalar curve + quaternion table)")
{
    FakeClipHost host;
    ClipEditorView view(DefaultAllocator(), host);

    // A scalar track -> the CurveCanvas path; a quaternion track -> the key-table path. Building
    // headlessly must not crash and each add is one undo step.
    view.AddTrack(u8"Transform", u8"position", propanim::TrackValueKind::Float3);
    view.AddTrack(u8"Transform", u8"rotation", propanim::TrackValueKind::Quat);
    CHECK(host.clip.tracks.Size() == 2);

    view.Rebuild(); // exercises BuildTrackRows for both kinds + the transport RefreshPreview sample
    CHECK(view.Root() != nullptr);

    // Undo both adds back to empty.
    host.commands.Undo();
    host.commands.Undo();
    CHECK(host.clip.tracks.Size() == 0);
}
