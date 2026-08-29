// Editor::PropertyAnimation - the `editor.propertyanimation:panel` partition.
//
// PropertyAnimationPanel is the PERSISTENT in-scene property-animation editor: a docked View - NOT a
// viewport tool mode - that the scene page owns for its
// whole lifetime and rests in a resizable splitter BELOW THE VIEWPORT. Being persistent avoids
// two defects a tool-mode would have: the editing view is never recreated mid-edit, so an
// undo command can never outlive it (a UAF), and the preview snapshot is re-taken whenever the
// track set changes (avoiding a stale snapshot). The panel IS the IClipEditorHost: it owns the editing clip
// (durable), routes edits through the scene page's command stack, loads / creates / saves the clip
// ASSET, owns the live-preview state, and hosts the Timeline scrubber above the shared ClipEditorView.
// The scene page calls Tick() / DrawOverlay() each frame (there is no IViewportTool seam anymore).

module;
#include "Core/Prelude.h"

export module editor.propertyanimation:panel;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.scene;
import foundation.render;
import foundation.content;
import foundation.propertyanimation;
import editor.core;
import editor.app;

import :clip_editor_view;

using namespace foundation::core;

// Module-local aliases (NOT exported - the primary owns the exported editor::ui / editor::propanim;
// see the ClipEditorView note on the GCC module-merger ICE from duplicate exported aliases).
namespace editor
{
    namespace ui = foundation::ui;
    namespace propanim = foundation::propertyanimation;
    namespace scene = foundation::scene;
}

export namespace editor
{
    // One animatable leaf property discovered on a component type: the track seed (component + dotted
    // property path + value kind) that "add track from selection" turns into a PropertyTrack.
    struct AnimatablePropertyInfo
    {
        String componentType;
        String propertyPath;
        propanim::TrackValueKind kind;
    };

    // Map a reflected leaf value-type to a TrackValueKind. Empty when the type is not animatable
    // (only f32 / Float3 / Color / Quaternion are). Pointer-identity against TypeOf<T>() - the same
    // idiom the inspector uses; there is deliberately no registry-wide inference helper.
    [[nodiscard]] Optional<propanim::TrackValueKind> InferTrackKind(const TypeInfo* leafType);

    // Enumerate the animatable leaf properties of a component type into `out` (own props; nested
    // structs recurse with dotted paths). Each entry is validated against ResolveBinding, so every
    // seed is one the runtime can actually drive.
    void CollectAnimatableProperties(const TypeInfo& componentType,
                                     Array<AnimatablePropertyInfo>& out);

    // The persistent in-scene property-animation editor panel.
    class PropertyAnimationPanel final : public ui::FlexLayout, public IClipEditorHost
    {
    public:
        // Borrows the edited scene, the scene page's command stack + entity selection, and the editor
        // context - all outlive the panel (the scene page owns them and the panel).
        PropertyAnimationPanel(EditorContext& editorCtx, scene::Scene& scene,
                               EditorCommandStack& commands, Selection<Guid>& selection);

        // === IClipEditorHost ===
        [[nodiscard]] propanim::PropertyAnimationClip& Clip() override { return m_clip; }
        [[nodiscard]] EditorCommandStack& Commands() override { return *m_commands; }
        String m_statusFlash;      // transient header status; empty = none
        f32 m_statusFlashSeconds = 0.0f;

        void MarkClipDirty() override
        {
            const bool wasDirty = m_dirty;
            m_dirty = true;
            if (!wasDirty)
            {
                RefreshHeader(); // show the dirty star as soon as the first edit lands
            }
            // A LIVE canvas edit (drag/add/delete) mutates keys without a view rebuild
            // (liveApplied) - resync the dopesheet markers now, or they lag until the next
            // full rebuild (UAT: "the timeline row does not update until I click another
            // track"). BuildLanes preserves selection by time and is cheap (float arrays).
            if (m_timeline.Get() != nullptr)
            {
                m_timeline->SetDuration(
                    Max(Max(m_clip.duration, m_clip.ComputeDuration()), 1.0f));
            }
            BuildLanes();
        }
        // The scrub moved (the Timeline scrubber, or a test). IGNORED while Playing - the transport
        // owns the playhead then (D6 Editing|Playing ownership). Drives live preview off the new time.
        void OnScrubTimeChanged(f32 time) override;
        // The view rebuilt its rows - resync the Timeline duration to the (possibly new) clip length.
        void OnClipViewRebuilt() override;
        // Undo/redo whole-clip apply, routed through the view (F1: the command holds this host, not a
        // view pointer).
        void ApplyClipState(const propanim::PropertyAnimationClip& state, bool rebuild) override;
        // Key-from-scene capture source: the primary selection's live value (ReadTrackTarget).
        [[nodiscard]] Variant ReadSceneValue(StringView componentType,
                                             StringView propertyPath) override;
        // Author the clip's duration (clamped to at least the last key); one undo step.
        void SetClipDuration(f32 seconds) override;
        // D1: hand the curve canvas the dopesheet Timeline's shared seconds<->pixels transform.
        [[nodiscard]] IClipEditorHost::TimeAxis ClipTimeTransform() const override;

        // === frame hooks (called by the scene page; replace the old IViewportTool seam) ===
        // Advances playback by dt when Playing (drives the playhead + preview); caches the EDIT/Simulate
        // gate and stands playback + preview down when locked. Only mutates widgets while playing, so an
        // idle panel triggers zero redraws (A6 damage gate).
        void Tick(f32 dt, bool editingLocked);
        void DrawOverlay(foundation::render::debug::DebugDraw& drawList); // preview marker overlay

        // === transport (D6: Editing | Playing) ===
        void Play();        // start/resume advancing from the current playhead (restarts if at the end)
        void TogglePause(); // pause/resume while Playing (no-op when stopped)
        void Stop();        // stop + rewind the playhead to 0 (shows the start pose)
        void SetLooping(bool loop);
        [[nodiscard]] bool IsLooping() const noexcept { return m_loop; }
        [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
        [[nodiscard]] bool IsPaused() const noexcept { return m_paused; }
        [[nodiscard]] f32 PlayheadTime() const noexcept { return m_playheadTime; }

        // === clip document management (workflow 2026-08-17: no scratch clip - the panel is
        // EMPTY until a clip asset is created or opened; every edit has a home) ===
        void LoadClip(const Guid& instanceId); // load an existing clip asset (unguarded)
        void SaveClip();                       // flatten -> write -> re-cook
        /// The pencil-claim / programmatic entry: dirty-guarded load (Save / Discard / Cancel
        /// prompt when a modified clip is open) + optionally bind `bindEntity` (the entity
        /// whose animator slot invoked the edit; Nil keeps the current binding).
        void RequestEditClip(const Guid& clipId, const Guid& bindEntity);
        [[nodiscard]] StringView ClipName() const noexcept { return m_clipName.AsView(); }
        [[nodiscard]] bool HasClip() const noexcept { return !m_clipId.IsNil(); }
        [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
        [[nodiscard]] const Guid& ClipId() const noexcept { return m_clipId; }

        // === the BOUND ENTITY (explicit binding - preview + key capture + track seeding all
        // target THIS entity, never the drifting live selection; "Use Selected" is the one
        // place selection enters) ===
        void BindEntity(const Guid& entityId);
        void BindSelectedEntity(); // bind the primary selection (flashes when none)
        [[nodiscard]] const Guid& BoundEntity() const noexcept { return m_boundEntity; }
        /// The scene page wires this to its EntityPickerDialog (the panel cannot depend on
        /// editor.scene - the dependency points the other way). (current, onPicked).
        Function<void(const Guid&, Function<void(const Guid&)>)> RequestEntityPick;

        // Add a track for each animatable property of the BOUND entity's components, as ONE undo
        // group. Returns the number of tracks added (0 = no bound entity / nothing animatable).
        usize AddTracksFromSelection(ClipEditorView& view);

        // Named EditorCtx (not Context) so it does not shadow the base ui::View::Context field.
        [[nodiscard]] EditorContext& EditorCtx() noexcept { return *m_editorCtx; }
        [[nodiscard]] ClipEditorView& View() noexcept { return *m_view; }
        [[nodiscard]] ui::toolkit::Timeline& Dopesheet() noexcept { return *m_timeline; }

        // Collapse/expand is owned by the enclosing BottomDock (A2 REVISED), not the panel itself.

        // === live preview - exposed for tests ===
        [[nodiscard]] bool IsPreviewing() const noexcept { return m_previewing; }
        void StopPreview(); // restore the snapshot + end the preview (idempotent)

    private:
        void ClearClip(); // drop the loaded clip (no asset written)
        void BuildChrome();
        void RefreshHeader();
        // Transient header status ("saved" / "save FAILED"); shown for a few seconds, then
        // RefreshHeader restores the clip line. Driven by Tick.
        void FlashStatus(StringView text);
        // Rebuild the dopesheet lanes from the clip (one lane per track) + preserve the selection by
        // time across the rebuild (D3/D4 commit-remap: keys have no id). Sizes the timeline pane.
        void BuildLanes();
        // Apply a dopesheet key drag: shift the selected keys' times by delta, one undo step, then
        // rebuild lanes + re-select the moved keys by their NEW time.
        void MoveSelectedKeys(f32 deltaSeconds);
        void RefreshTransportButtons();     // sync transport button labels to the state
        void Advance(f32 dt);               // move the playhead + drive preview (called while Playing)
        void PreviewSelected(f32 time);     // preview the clip at `time` on the BOUND entity
        void StopPlaybackInternal();        // stop advancing WITHOUT rewinding (Simulate override)

        // Header actions.
        void OnCreateClip(); // dirty-guarded -> AssetCreateDialog -> create + load
        void OnOpenClip();   // dirty-guarded -> AssetPickerDialog -> load
        void OnAddFromSelection();
        void OnAddTrackMenu(); // "+ Track": a property picker (the bound entity's animatable leaves)
        void OnSave();
        // Run `proceed` now, or - when a modified clip is loaded - after a Save / Discard /
        // Cancel prompt (Cancel drops it). The dirty guard every load/create path shares.
        void RunDirtyGuarded(Function<void()> proceed);
        void RefreshClipStateUI(); // empty state <-> editor body (exclusive; workflow ruling)
        void RefreshEntitySlot();  // the bound-entity name in the header slot

        // The animatable-property seeds for the BOUND entity (Transform TRS + each reflected
        // component's animatable leaves). Shared by + Tracks (adds all) + the + Track picker.
        [[nodiscard]] Array<AnimatablePropertyInfo> CollectSelectionTrackSeeds();
        [[nodiscard]] bool ClipHasTrack(StringView componentType, StringView propertyPath) const;

        // One property captured before a transient preview write, so it can be restored EXACTLY
        // (re-resolved each time, never a cached instance - the entity.get lesson).
        struct PreviewSnapshotEntry
        {
            String componentType;
            String propertyPath;
            Variant value;
        };
        void PreviewAt(scene::EntityHandle entity, f32 time); // snapshot-if-needed + write sampled values
        void SnapshotEntity(scene::EntityHandle entity);
        // The identity of the tracks a snapshot was taken against ("component|path" per track). A change
        // (add / remove / retarget a track) invalidates the snapshot so it is re-taken (the #6 fix).
        [[nodiscard]] Array<String> TrackIdentity() const;
        [[nodiscard]] static bool SameIdentity(const Array<String>& a, const Array<String>& b);
        [[nodiscard]] scene::ComponentManagerBase* FindManagerByComponentTypeName(StringView name);

        // Read / write a track target on an entity, handling the built-in "Transform" component
        // (the scene transform, written through Set/GetLocalTransform) as well as reflected
        // components. Empty Variant = unresolved. Shared by preview, snapshot and restore.
        [[nodiscard]] Variant ReadTrackTarget(scene::EntityHandle entity, StringView componentType,
                                              StringView propertyPath);
        void WriteTrackTarget(scene::EntityHandle entity, StringView componentType,
                              StringView propertyPath, const Variant& value);

        EditorContext* m_editorCtx;
        scene::Scene* m_scene;          // borrowed (the edited scene)
        EditorCommandStack* m_commands; // borrowed (the scene page's stack)
        Selection<Guid>* m_selection;   // borrowed (the scene page's entity selection)
        propanim::PropertyAnimationClip m_clip; // the editing model (durable across the panel's life)
        Guid m_clipId;                          // the clip asset being edited (Nil = none loaded)
        String m_clipName;
        bool m_dirty = false;

        // Chrome + widgets (persistent - built once in the constructor).
        RefPtr<ui::Label> m_clipLabel;
        RefPtr<ui::Button> m_addTrackButton; // anchors the "+ Track" property-picker menu
        RefPtr<ui::Button> m_playButton;
        RefPtr<ui::Button> m_pauseButton;
        RefPtr<ui::Button> m_loopButton;
        RefPtr<ui::toolkit::Timeline> m_timeline;
        RefPtr<ui::FlexLayoutParams> m_timelineParams; // updated to size the timeline pane to its lanes
        RefPtr<ui::FlexLayout> m_header;     // clip/entity chrome (collapsed while empty)
        RefPtr<ui::FlexLayout> m_emptyState; // "no clip" message + Create/Open (exclusive)
        RefPtr<ui::Label> m_entityLabel;     // the bound-entity slot's name readout
        RefPtr<ui::FlexLayout> m_body; // transport + Timeline + ClipEditorView
        UniquePtr<ClipEditorView> m_view;
        Guid m_boundEntity; // the EXPLICIT preview/key/seed target (Nil = unbound)

        // Dopesheet lane bookkeeping. m_laneKeyTimes[lane] is the sorted key time each marker on that
        // lane represents (parallel to the Timeline's lanes), so a (lane,index) selection maps to a
        // time for re-selection across rebuilds.
        struct ReselectMark
        {
            u32 lane = 0;
            f32 time = 0.0f;
        };
        Array<Array<f32>> m_laneKeyTimes;
        Array<ReselectMark> m_reselectTimes; // the moved keys' NEW times (consumed by the next BuildLanes)
        bool m_haveReselect = false;

        // Transport (editor-local; D6 Editing|Playing). The loop toggle defaults to Loop, matching a
        // fresh PropertyAnimatorComponent; it is not seeded from a specific bound animator instance
        // (that would couple the clip editor to engine.animation).
        bool m_playing = false;
        bool m_paused = false;
        bool m_loop = true;
        f32 m_playheadTime = 0.0f; // the transport clock (mirrored to the Timeline widget)

        // Preview state (transient; NEVER dirties the document or goes through undo).
        bool m_editingLocked = false; // last Tick's Simulate/Play gate (no preview when true)
        bool m_previewing = false;
        scene::EntityHandle m_previewEntity; // the entity currently being previewed
        f32 m_previewTime = 0.0f;
        Array<PreviewSnapshotEntry> m_snapshot; // pre-preview values, restored on stop
        Array<String> m_snapshotIdentity;       // the track identity the snapshot was taken against (#6)
    };
}
