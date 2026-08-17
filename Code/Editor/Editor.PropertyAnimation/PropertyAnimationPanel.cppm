// Editor::PropertyAnimation - the `editor.propertyanimation:panel` partition.
//
// PropertyAnimationPanel is the PERSISTENT in-scene property-animation editor (property-animation.md
// editor redesign, P1b): a docked View - NOT a viewport tool mode - that the scene page owns for its
// whole lifetime and rests in a resizable splitter BELOW THE VIEWPORT. Being persistent is the fix for
// the two review-pass-10 defects the tool-mode had: the editing view is never recreated mid-edit, so an
// undo command can never outlive it (the #5 UAF), and the preview snapshot is re-taken whenever the
// track set changes (the #6 stale snapshot). The panel IS the IClipEditorHost: it owns the editing clip
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
        void MarkClipDirty() override { m_dirty = true; }
        // The scrub moved (the Timeline scrubber, or a test). IGNORED while Playing - the transport
        // owns the playhead then (D6 Editing|Playing ownership). Drives live preview off the new time.
        void OnScrubTimeChanged(f32 time) override;
        // The view rebuilt its rows - resync the Timeline duration to the (possibly new) clip length.
        void OnClipViewRebuilt() override;
        // Undo/redo whole-clip apply, routed through the view (F1: the command holds this host, not a
        // view pointer).
        void ApplyClipState(const propanim::PropertyAnimationClip& state, bool rebuild) override;

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

        // === clip document management ===
        void NewClip();                        // create a clip asset in the project + load it
        void LoadClip(const Guid& instanceId); // load an existing clip asset
        void SaveClip();                       // flatten -> write -> re-cook
        [[nodiscard]] StringView ClipName() const noexcept { return m_clipName.AsView(); }
        [[nodiscard]] bool HasClip() const noexcept { return !m_clipId.IsNil(); }
        [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
        [[nodiscard]] const Guid& ClipId() const noexcept { return m_clipId; }

        // Add a track for each animatable property of the selected entity's components, as ONE undo
        // group. Returns the number of tracks added (0 = nothing selected / nothing animatable).
        usize AddTracksFromSelection(ClipEditorView& view);

        // Named EditorCtx (not Context) so it does not shadow the base ui::View::Context field.
        [[nodiscard]] EditorContext& EditorCtx() noexcept { return *m_editorCtx; }
        [[nodiscard]] ClipEditorView& View() noexcept { return *m_view; }

        // Collapse/expand is owned by the enclosing BottomDock (A2 REVISED), not the panel itself.

        // === live preview - exposed for tests ===
        [[nodiscard]] bool IsPreviewing() const noexcept { return m_previewing; }
        void StopPreview(); // restore the snapshot + end the preview (idempotent)

    private:
        void ClearClip(); // drop the loaded clip (no asset written)
        void BuildChrome();
        void RefreshHeader();
        void RefreshTransportButtons();     // sync transport button labels to the state
        void Advance(f32 dt);               // move the playhead + drive preview (called while Playing)
        void PreviewSelected(f32 time);     // preview the clip at `time` on the selected entity
        void StopPlaybackInternal();        // stop advancing WITHOUT rewinding (Simulate override)

        // Header actions.
        void OnNew();
        void OnPick();
        void OnAddFromSelection();
        void OnSave();

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
        RefPtr<ui::Button> m_playButton;
        RefPtr<ui::Button> m_pauseButton;
        RefPtr<ui::Button> m_loopButton;
        RefPtr<ui::toolkit::Timeline> m_timeline;
        RefPtr<ui::FlexLayout> m_body; // transport + Timeline + ClipEditorView
        UniquePtr<ClipEditorView> m_view;

        // Transport (editor-local; D6 Editing|Playing). The loop toggle defaults to Loop, matching a
        // fresh PropertyAnimatorComponent; seeding it from a specific bound animator instance is a
        // deferred nicety (would couple the clip editor to engine.animation).
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
