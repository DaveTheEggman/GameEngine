// Editor::PropertyAnimation - the `editor.propertyanimation:tool` partition.
//
// The IN-SCENE authoring mode (property-animation.md Phase H3). PropertyAnimationTool is a viewport
// tool (editor.viewporttools) AND the clip-editor host for its docked panel: it owns the editing
// clip (DURABLE - it survives panel recreation, so the view is free to be rebuilt), routes edits
// through the scene page's command stack, and loads / creates / saves the clip ASSET. Its panel
// (built by PropertyAnimationPanelProvider) is clip chrome - new / pick / add-from-selection / save -
// above the SAME shared ClipEditorView the standalone page uses, so authoring matches in both places.

module;
#include "Core/Prelude.h"

export module editor.propertyanimation:tool;

import foundation.core;
import foundation.ui;
import foundation.scene;
import foundation.render;
import foundation.content;
import foundation.propertyanimation;
import editor.core;
import editor.app;
import editor.viewporttools;

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

    // The in-scene property-animation authoring tool.
    class PropertyAnimationTool final : public IViewportTool, public IClipEditorHost
    {
    public:
        PropertyAnimationTool(const ViewportToolHostContext& ctx, EditorContext& editorCtx);

        // === IViewportTool ===
        [[nodiscard]] StringView Id() const override { return u8"property.animation"; }
        [[nodiscard]] StringView DisplayName() const override { return u8"Property Animation"; }
        bool Update(const ViewportToolInput& input) override; // caches the EDIT/Simulate gate
        void OnDeactivate() override;                         // restore any live preview
        void Draw(foundation::render::debug::DebugDraw& drawList) override; // preview overlay marker
        [[nodiscard]] StringView StatusText() const override { return m_status.AsView(); }

        // === IClipEditorHost ===
        [[nodiscard]] propanim::PropertyAnimationClip& Clip() override { return m_clip; }
        [[nodiscard]] EditorCommandStack& Commands() override { return *m_commands; }
        void MarkClipDirty() override { m_dirty = true; }
        // The scrub moved: drive the runtime evaluation path onto the selected entity (Phase H4).
        void OnScrubTimeChanged(f32 time) override;

        // === clip document management ===
        void NewClip();                        // create a clip asset in the project + load it
        void LoadClip(const Guid& instanceId); // load an existing clip asset
        void SaveClip();                       // flatten -> write -> re-cook
        [[nodiscard]] StringView ClipName() const noexcept { return m_clipName.AsView(); }
        [[nodiscard]] bool HasClip() const noexcept { return !m_clipId.IsNil(); }
        [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }
        [[nodiscard]] const Guid& ClipId() const noexcept { return m_clipId; }

        // Add a track for each animatable property of the selected entity's components, as ONE
        // undo group. Returns the number of tracks added (0 = nothing selected / nothing animatable).
        usize AddTracksFromSelection(ClipEditorView& view);

        [[nodiscard]] EditorContext& Context() noexcept { return *m_editorCtx; }

        // === live preview (Phase H4) - exposed for tests ===
        [[nodiscard]] bool IsPreviewing() const noexcept { return m_previewing; }
        void StopPreview(); // restore the snapshot + end the preview (idempotent)

    private:
        void ClearClip(); // drop the loaded clip (no asset written)

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
        [[nodiscard]] scene::ComponentManagerBase* FindManagerByComponentTypeName(StringView name);

        // Read / write a track target on an entity, handling the built-in "Transform" component
        // (the scene transform, written through Set/GetLocalTransform) as well as reflected
        // components. Empty Variant = unresolved. Shared by preview, snapshot and restore.
        [[nodiscard]] Variant ReadTrackTarget(scene::EntityHandle entity, StringView componentType,
                                              StringView propertyPath);
        void WriteTrackTarget(scene::EntityHandle entity, StringView componentType,
                              StringView propertyPath, const Variant& value);

        EditorContext* m_editorCtx;
        scene::Scene* m_scene;         // borrowed from the host context (the edited scene)
        EditorCommandStack* m_commands; // borrowed (the scene page's stack)
        Selection<Guid>* m_selection;   // borrowed (the scene page's entity selection)
        propanim::PropertyAnimationClip m_clip; // the editing model (durable across panel rebuilds)
        Guid m_clipId;                          // the clip asset being edited (Nil = none loaded)
        String m_clipName;
        String m_status;
        bool m_dirty = false;

        // Preview state (transient; NEVER dirties the document or goes through undo).
        bool m_editingLocked = false;         // last Update's Simulate/Play gate (no preview when true)
        bool m_previewing = false;
        scene::EntityHandle m_previewEntity;  // the entity currently being previewed
        f32 m_previewTime = 0.0f;
        Array<PreviewSnapshotEntry> m_snapshot; // pre-preview values, restored on stop
    };

    // Contributes the tool to each scene viewport (registered from RegisterPropertyAnimationEditor).
    class PropertyAnimationToolProvider final : public IViewportToolProvider
    {
    public:
        explicit PropertyAnimationToolProvider(EditorContext& editorCtx) : m_editorCtx(&editorCtx) {}
        void CreateTools(ViewportToolManager& manager,
                         const ViewportToolHostContext& context) override;

    private:
        EditorContext* m_editorCtx;
    };

    // Builds the tool's docked panel (clip chrome + the shared ClipEditorView).
    class PropertyAnimationPanelProvider final : public IViewportToolPanelProvider
    {
    public:
        explicit PropertyAnimationPanelProvider(EditorContext& editorCtx) : m_editorCtx(&editorCtx) {}
        [[nodiscard]] StringView ToolId() const override { return u8"property.animation"; }
        [[nodiscard]] RefPtr<foundation::ui::View>
        CreatePanel(IViewportTool& tool, const ViewportToolHostContext& context) override;

    private:
        EditorContext* m_editorCtx;
    };
}
