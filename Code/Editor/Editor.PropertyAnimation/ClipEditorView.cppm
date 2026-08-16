// Editor::PropertyAnimation - the `editor.propertyanimation:clip_editor_view` partition.
//
// ClipEditorView (property-animation.md Phase H2): the SHARED clip-editing surface - a scrollable
// track list (component + property + kind), a keyframe TABLE per quaternion track and an interactive
// CurveCanvas per scalar track, plus a transport row (scrub + length + sampled-value readout). It is
// the same view whether it is docked in the standalone clip PAGE or in the in-scene tool PANEL (H3);
// both host it over the IClipEditorHost seam. The view owns ZERO document policy - it reads and
// writes the host's clip, pushes every edit through the host's command stack (one undo step per
// discrete edit, one per curve-drag gesture), and tells the host when the scrub time moves so the
// host can drive live preview (H4). Persistence, cooking and page chrome stay in the host.

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

        /// The view finished (re)building its rows - the host may refresh chrome (e.g. its page
        /// toolbar's undo/redo state). Default no-op.
        virtual void OnClipViewRebuilt() {}
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

        /// Append a track (component + property + value kind) as ONE undoable edit. Drives the
        /// "+ Track" button and the in-scene tool's "add track from selection" (H3).
        void AddTrack(StringView componentType, StringView propertyPath, propanim::TrackValueKind kind);

        [[nodiscard]] f32 ScrubTime() const noexcept { return m_scrubTime; }
        [[nodiscard]] f32 EditDuration() const noexcept { return m_editDuration; }
        [[nodiscard]] IClipEditorHost& Host() noexcept { return *m_host; }

    private:
        // One undoable step over a whole-clip snapshot (the clip is small data). Applying a state
        // replaces the host's clip and rebuilds the view.
        class ClipEditCommand final : public IEditorCommand
        {
        public:
            ClipEditCommand(ClipEditorView& view, propanim::PropertyAnimationClip before,
                            propanim::PropertyAnimationClip after)
                : m_view(&view), m_before(Move(before)), m_after(Move(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_view->ApplyState(m_after);
                return true;
            }
            void Undo() override { m_view->ApplyState(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"propanim-clip-edit"; }

        private:
            ClipEditorView* m_view;
            propanim::PropertyAnimationClip m_before;
            propanim::PropertyAnimationClip m_after;
        };

        void ApplyState(const propanim::PropertyAnimationClip& state);

        // Snapshot -> mutate -> push. `fn` edits a COPY that becomes the new clip (one undo step).
        template <typename Fn>
        void Mutate(Fn&& fn)
        {
            propanim::PropertyAnimationClip before = m_host->Clip();
            propanim::PropertyAnimationClip after = m_host->Clip();
            fn(after);
            after.duration = after.ComputeDuration();
            (void)m_host->Commands().Execute(UniquePtr<IEditorCommand>(
                DefaultAllocator().New<ClipEditCommand>(*this, Move(before), Move(after)),
                DefaultAllocator()));
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
        void BuildTrackRows(usize trackIndex);
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
        RefPtr<ui::Label> m_preview; // sampled values at the scrub time
        f32 m_scrubTime = 0.0f;
        f32 m_editDuration = 1.0f;   // the canvas time-axis scale (clip length); keys normalize by it
        propanim::PropertyAnimationClip m_gestureBefore; // undo snapshot captured on OnEditBegin
    };
}
