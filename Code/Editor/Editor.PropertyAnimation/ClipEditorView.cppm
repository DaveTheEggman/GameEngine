// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::PropertyAnimation - the `editor.propertyanimation:clip_editor_view` partition.
//
// ClipEditorView: the SHARED clip-editing surface - a scrollable
// track list (component + property + kind), a keyframe TABLE per quaternion track and an interactive
// CurveCanvas per scalar track, plus a transport row (scrub + length + sampled-value readout). It is
// the same view whether it is docked in the standalone clip PAGE or in the in-scene tool PANEL;
// both host it over the IClipEditorHost seam. The view owns ZERO document policy - it reads and
// writes the host's clip, pushes every edit through the host's command stack (one undo step per
// discrete edit, one per curve-drag gesture), and tells the host when the scrub time moves so the
// host can drive live preview. Persistence, cooking and page chrome stay in the host.

module;
#include "Core/Prelude.h"

export module editor.propertyanimation:clip_editor_view;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.propertyanimation;
import editor.core;

using namespace foundation::core;

// Module-local aliases (NOT exported): the primary module `editor.propertyanimation` exports its own
// `editor::ui` / `editor::propanim` aliases, and two units of the module exporting the same alias
// risks the GCC module-merger ICE the ViewportTools note records. These are just for compiling this
// partition's signatures; consumers get the aliases from the primary.
namespace editor
{
    namespace ui = foundation::ui;
    namespace propanim = foundation::propertyanimation;
}

export namespace editor
{
    /// The seam a ClipEditorView edits against. The host owns the clip storage, the undo stack, the
    /// dirty flag and (optionally) live preview; the view owns all the widgets and edit logic.
    class IClipEditorHost
    {
    public:
        virtual ~IClipEditorHost() = default;

        /// The editing model the view reads and writes in place. Mutable: curve-drag write-backs
        /// edit it live, then the view records one undo step spanning the whole gesture.
        [[nodiscard]] virtual propanim::PropertyAnimationClip& Clip() = 0;

        /// The undo stack the view pushes its clip-edit commands onto.
        [[nodiscard]] virtual EditorCommandStack& Commands() = 0;

        /// Flag the document dirty (a live write-back during a gesture, before the gesture commits).
        virtual void MarkClipDirty() = 0;

        /// The scrub time moved (H4 hook: the host may drive a live preview of the selected entity).
        /// Default no-op - the standalone page only updates its own readout.
        virtual void OnScrubTimeChanged(f32 time) { (void)time; }

        /// Set the clip's authored duration (the play/loop bound + timeline extent). The host clamps it
        /// to at least the last key (a duration can't cut a key off) and commits it as one undo step.
        /// Default no-op. Sedulous parity: the clip length is authorable, not just key-derived.
        virtual void SetClipDuration(f32 seconds) { (void)seconds; }

        /// The view finished (re)building its rows - the host may refresh chrome (e.g. its page
        /// toolbar's undo/redo state). Default no-op.
        virtual void OnClipViewRebuilt() {}

        /// The shared dopesheet time transform (D1): pixels-per-second + scroll + the label-column
        /// gutter width. The curve canvas reads this so it follows the Timeline's zoom/scroll and aligns
        /// under the dopesheet lanes. Default = a static 100 px/s axis, no scroll, no gutter.
        struct TimeAxis
        {
            f32 pixelsPerSecond = 100.0f;
            f32 scrollSeconds = 0.0f;
            f32 labelColumnWidth = 0.0f;
        };
        [[nodiscard]] virtual TimeAxis ClipTimeTransform() const { return {}; }

        /// Read the CURRENT scene value of (componentType, propertyPath) from the bound entity
        /// (the panel's primary selection) - the key-from-scene capture source. Empty Variant
        /// when unresolvable (no entity / no component / bad path); the caller skips then.
        [[nodiscard]] virtual Variant ReadSceneValue(StringView componentType,
                                                     StringView propertyPath)
        {
            (void)componentType;
            (void)propertyPath;
            return {};
        }

        /// Apply a whole-clip state (an undo/redo step) to the host's clip and refresh the editing
        /// surface. The ClipEditCommand routes through THIS seam - never a view pointer - so it depends
        /// only on the DURABLE host, not a recreatable view. The default sets the
        /// clip only; a host owning a live view overrides to also rebuild it.
        virtual void ApplyClipState(const propanim::PropertyAnimationClip& state, bool rebuild)
        {
            Clip() = state;
            (void)rebuild;
        }
    };

    /// The shared clip-editing view. Construct with a host; mount Root() wherever the host wants it.
    class ClipEditorView
    {
    public:
        explicit ClipEditorView(IClipEditorHost& host);

        /// The scrollable rows container (transport + tracks). The host wraps this with its chrome.
        [[nodiscard]] ui::View* Root() { return m_scroll.Get(); }

        /// Rebuild the rows from the host's current clip (immediate). Called after an undo/redo
        /// replaces the clip, and once at construction.
        void Rebuild();

        /// Defer a Rebuild through the UI mutation queue (mid-event-dispatch safe).
        void RequestRebuild();

        /// Resync to the host's CURRENT clip after it is replaced wholesale (a new/loaded document,
        /// not an edit): recompute the canvas time axis from the clip length, reset the scrub, and
        /// rebuild. (An in-place edit uses RequestRebuild; this also rescales the axis.)
        void ResetForClip();

        /// Append a track (component + property + value kind) as ONE undoable edit. Drives the
        /// "+ Track" button and the in-scene tool's "add track from selection" (H3).
        void AddTrack(StringView componentType, StringView propertyPath, propanim::TrackValueKind kind);

        /// Replace the host's clip with `state` and (optionally) rebuild the rows. The host's
        /// ApplyClipState forwards here so undo/redo never needs a view pointer (Fable F1).
        void ApplyState(const propanim::PropertyAnimationClip& state, bool rebuild = true);

        /// Push a whole-clip before/after as ONE undo step (the dopesheet key-drag commit uses this;
        /// the view's own field edits use the internal Mutate). Recomputes duration + rebuilds on apply.
        void PushClipEdit(propanim::PropertyAnimationClip before, propanim::PropertyAnimationClip after);

        [[nodiscard]] f32 ScrubTime() const noexcept { return m_scrubTime; }
        [[nodiscard]] f32 EditDuration() const noexcept { return m_editDuration; }

        /// Set the scrub time from the host's Timeline scrubber and refresh the sampled-value readout.
        /// The panel drives this from the Timeline widget (the numeric scrub field is retired); the
        /// host separately runs live preview off OnScrubTimeChanged.
        void SetScrubTime(f32 t);
        [[nodiscard]] IClipEditorHost& Host() noexcept { return *m_host; }

        /// Select a KEY: drives the value readout AND the keyframe inspector (time + typed
        /// value fields). Selection is selection wherever it happens - the curve canvas pushes
        /// its picks; the panel pushes dopesheet picks (channel -1 = the whole track at that
        /// key time, since a dopesheet diamond merges channels).
        void ShowSelectedKey(usize trackIndex, i32 channel, f32 time);
        void ClearSelectedKey();

        /// Select a TRACK (the dopesheet IS the track list - Sedulous shape): the strip, the
        /// keyframe inspector, and the single curve canvas all show this track. -1 = none.
        void SetSelectedTrack(i32 trackIndex);
        [[nodiscard]] i32 SelectedTrack() const noexcept { return m_selectedTrack; }

        /// Push the host's current shared time transform (D1) into the live curve canvas so it follows
        /// the Timeline's zoom/scroll. Called by the panel on the Timeline's OnViewChanged.
        void SyncCanvasTransform();

    private:
        // One undoable step over a whole-clip snapshot (the clip is small data). Applying a state
        // replaces the host's clip and rebuilds the view.
        class ClipEditCommand final : public IEditorCommand
        {
        public:
            // `liveApplied` = the `after` state is ALREADY applied to the clip AND visible on screen
            // (a curve-canvas drag mutates live). For those, the first Execute must NOT rebuild - a
            // rebuild recreates the canvas and drops the selected key + its tangent handles. Discrete
            // edits (add/remove track, field edits) leave it false so the first Execute rebuilds.
            ClipEditCommand(IClipEditorHost& host, propanim::PropertyAnimationClip before,
                            propanim::PropertyAnimationClip after, bool liveApplied = false)
                : m_host(&host), m_before(Move(before)), m_after(Move(after)), m_liveApplied(liveApplied)
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_host->ApplyClipState(m_after, /*rebuild=*/!m_liveApplied);
                m_liveApplied = false; // a later redo DOES rebuild (the canvas is stale by then)
                return true;
            }
            void Undo() override { m_host->ApplyClipState(m_before, /*rebuild=*/true); }
            [[nodiscard]] StringView TypeId() const override { return u8"propanim-clip-edit"; }

        private:
            IClipEditorHost* m_host; // the DURABLE seam, never the recreatable view
            propanim::PropertyAnimationClip m_before;
            propanim::PropertyAnimationClip m_after;
            bool m_liveApplied;
        };

        // Snapshot -> mutate -> push. `fn` edits a COPY that becomes the new clip (one undo step).
        template <typename Fn>
        void Mutate(Fn&& fn)
        {
            propanim::PropertyAnimationClip before = m_host->Clip();
            propanim::PropertyAnimationClip after = m_host->Clip();
            fn(after);
            // Preserve an authored (longer) duration; grow it if keys now extend past it. Never shrink
            // below the last key.
            after.duration = Max(after.duration, after.ComputeDuration());
            (void)m_host->Commands().Execute(UniquePtr<IEditorCommand>(
                foundation::core::DefaultAllocator().New<ClipEditCommand>(*m_host, Move(before), Move(after)),
                foundation::core::DefaultAllocator()));
        }

        // --- UI construction (bodies in the impl unit) ---
        [[nodiscard]] RefPtr<ui::FlexLayout> MakeRow(f32 indent, f32 height = 24.0f);
        ui::Button* MakeButton(ui::FlexLayout& row, StringView label, f32 width,
                               Function<void()> onClick);
        void AddLabel(ui::FlexLayout& row, StringView text, f32 grow = 0.0f, f32 width = 0.0f);
        void AddTextField(ui::FlexLayout& row, StringView value, Function<void(StringView)> commit,
                          f32 width);
        void AddFloatField(ui::FlexLayout& row, f32 value, Function<void(f32)> commit,
                           f32 width = 56.0f);
        void BuildTransportRow();
        // The Sedulous shape: the dopesheet is the track list, so the area below builds for the
        // SELECTED track only - a strip (path/kind/interp/Key/remove), the keyframe inspector
        // (time + typed value fields for the selected key), and ONE curve canvas.
        void BuildSelectedTrackStrip();
        void BuildKeyInspectorHost();
        // Quat tracks have no canvas - the keys strip makes their keyframes VISIBLE and
        // clickable (chips select into the inspector; Del there removes).
        void BuildQuatKeysRow(usize trackIndex);
        void RefreshKeyInspector();   // rebuilds ONLY the inspector row's children (no canvas loss)
        void RequestInspectorRefresh(); // deferred via the UI mutation queue (mid-dispatch safe)
        void KeyTrackFromScene(usize trackIndex); // capture the scene value at the playhead
        void KeyAllFromScene();
        void RefreshPreview(); // update the sampled-values readout at the current scrub time

        void AddCurveCanvas(usize trackIndex);
        void PushTrackToCanvas(usize trackIndex, ui::toolkit::CurveCanvas& canvas);
        void WriteBackTrack(usize trackIndex, ui::toolkit::CurveCanvas& canvas);

        [[nodiscard]] static StringView KindName(propanim::TrackValueKind kind);
        [[nodiscard]] static StringView InterpName(CurveKeyInterpolation interp);
        [[nodiscard]] static ui::toolkit::CurveInterpolation ClipToCanvasInterp(CurveKeyInterpolation i);
        [[nodiscard]] static CurveKeyInterpolation CanvasToClipInterp(ui::toolkit::CurveInterpolation i);

        [[nodiscard]] propanim::PropertyAnimationClip& Clip() { return m_host->Clip(); }

        IClipEditorHost* m_host;
        RefPtr<ui::ScrollView> m_scroll;
        RefPtr<ui::FlexLayout> m_rows;
        RefPtr<ui::Label> m_preview; // sampled values at the scrub time (+ the selected key)
        bool m_previewSelActive = false; // a key is selected somewhere (canvas or dopesheet)
        usize m_previewSelTrack = 0;
        i32 m_previewSelChannel = -1;    // -1 = whole track (a dopesheet diamond merges channels)
        f32 m_previewSelTime = 0.0f;
        f32 m_scrubTime = 0.0f;
        f32 m_editDuration = 1.0f;   // the canvas time-axis scale (clip length); keys normalize by it
        propanim::PropertyAnimationClip m_gestureBefore; // undo snapshot captured on OnEditBegin
        bool m_gestureDirty = false; // a canvas gesture actually changed a key (vs a bare select-click)
        i32 m_selectedTrack = -1;    // the dopesheet-selected track (-1 = none)
        RefPtr<ui::FlexLayout> m_inspectorRow; // persistent host; children rebuilt per selection
        ui::toolkit::CurveCanvas* m_curveCanvas = nullptr; // the live canvas (owned by the row tree); D1 sync target
    };
}
