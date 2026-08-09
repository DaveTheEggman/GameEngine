// Draconic::EditorScene - :animation_clip_page partition.
//
// AnimationClipEditorPage (editor-pages-gap.md, bespoke pass #4's lighter half): preview + light
// authoring for an AnimationClipAsset. A skeleton-wireframe viewport plays the COOKED clip product
// (pick a Skeleton, scrub or play the timeline); the inspector edits the source's loop flag and
// its animation EVENTS (time + name rows, add/remove) with blob-snapshot undo, and reads out the
// stats (duration, tracks, keys). Save writes the asset + recooks so bound proxies hot-swap.
//
// Clips are IMPORTED (model importer), so there is no New-Asset creator here.

module;
#include "Core/Prelude.h"

export module draconic.editor.scene:animation_clip_page;

import draconic.core;
import draconic.content;
import draconic.rhi;
import draconic.graphics;
import draconic.shell;
import draconic.runtime;
import draconic.runtime.client;
import draconic.scene;
import draconic.engine.scene;
import draconic.animation;
import draconic.animation.resource;
import draconic.animation.pipeline;
import draconic.resource;
import draconic.render;
import draconic.engine.render;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.ui.runtime;
import draconic.ui.viewport;
import draconic.vg.renderer;
import draconic.editor.core;
import draconic.editor.app;
import :camera;
import :animation_graph_page; // DrawSkeletonWireframe (shared preview helper)

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace vg = foundation::vg;
    namespace scene = foundation::scene;
    namespace render = foundation::render;
    namespace animation = foundation::animation;

    class AnimationClipEditorPage final : public app::UIEditorPage
    {
    public:
        AnimationClipEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                                ui::runtime::UIHost& uiHost, foundation::content::Instance& instance);

        [[nodiscard]] foundation::ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }
        [[nodiscard]] Status Save() override;

        void OnUpdate(runtime::IApplicationHost&, f32 dt) override;
        void OnRenderWindow(runtime::IApplicationHost&,
                            foundation::graphics::FrameContext& frame) override;
        void OnClose() override;

        // Record a coalesced undo step for an in-place edit that already happened (merge by key).
        void CommitEdit(StringView mergeKey);

    private:
        class EditClipCommand final : public IEditorCommand
        {
        public:
            EditClipCommand(AnimationClipEditorPage& page, StringView mergeKey, Array<byte> before,
                            Array<byte> after)
                : m_page(&page), m_mergeKey(mergeKey), m_before(Move(before)), m_after(Move(after))
            {
            }
            [[nodiscard]] bool Execute() override
            {
                m_page->ApplyAssetBlob(m_after);
                return true;
            }
            void Undo() override { m_page->ApplyAssetBlob(m_before); }
            [[nodiscard]] StringView TypeId() const override { return u8"edit_animclip"; }
            [[nodiscard]] bool MergeInto(IEditorCommand& previous) override
            {
                auto& prev = static_cast<EditClipCommand&>(previous);
                if (prev.m_page != m_page || prev.m_mergeKey.AsView() != m_mergeKey.AsView())
                {
                    return false;
                }
                prev.m_after = Move(m_after);
                return true;
            }

        private:
            AnimationClipEditorPage* m_page;
            String m_mergeKey;
            Array<byte> m_before;
            Array<byte> m_after;
        };

        void BuildPreviewScene();
        void PickPreviewSkeleton();
        void RebuildGrid(); // stats + loop flag + events editor
        void UpdatePreview(f32 dt);
        void EnsureViewportBound();

        [[nodiscard]] Array<byte> SnapshotAsset() const;
        void ApplyAssetBlob(const Array<byte>& blob);
        void QueueStructural(StringView undoKey, Function<void()> mutate);

        [[nodiscard]] ui::UIContext* Ctx() const;

        EditorContext* m_context = nullptr;
        runtime::IApplicationHost* m_host = nullptr;
        ui::runtime::UIHost* m_uiHost = nullptr;
        String m_title;

        RefPtr<pipeline::AnimationClipAsset> m_asset;
        foundation::resource::Proxy<animation::AnimationClip> m_clip; // cooked product (hot-swaps)

        // preview world (debug-draw only)
        engine::scene::SceneSubsystem* m_scenes = nullptr;
        scene::SceneManager m_sceneManager;
        engine::render::RenderSubsystem* m_render = nullptr;
        scene::Scene* m_scene = nullptr;
        EditorCamera m_camera;
        UniquePtr<foundation::shell::InputRouter> m_router;
        RefPtr<ui::viewport::ViewportView> m_viewport;
        foundation::graphics::RenderWindow* m_hostWindow = nullptr;

        RefPtr<ui::Button> m_skeletonButton;
        RefPtr<ui::Button> m_playButton;
        RefPtr<ui::Slider> m_timeSlider; // normalized [0..1] scrub
        RefPtr<ui::Label> m_timeLabel;
        RefPtr<ui::toolkit::PropertyGrid> m_grid;
        RefPtr<foundation::ui::View> m_content;

        Guid m_skeletonGuid{};
        foundation::resource::Proxy<animation::Skeleton> m_skeleton;
        Array<animation::BoneTransform> m_poseScratch;
        Array<Float4x4> m_worldScratch;
        Array<byte> m_undoBaseline;
        f32 m_time = 0.0f; // seconds into the clip
        bool m_playing = true;
        bool m_scrubbing = false; // slider writes m_time; playback writes the slider
    };

    class AnimationClipPageFactory final : public IEditorPageFactory
    {
    public:
        AnimationClipPageFactory(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost)
        {
        }

        [[nodiscard]] const TypeInfo* PrimaryType() const override;
        [[nodiscard]] UniquePtr<EditorPage>
        CreatePage(EditorContext& context, foundation::content::Instance& instance) override;

    private:
        runtime::IApplicationHost* m_host;
        ui::runtime::UIHost* m_uiHost;
    };

    // Registers the AnimationClip page factory (no creator - clips come from import).
    void RegisterAnimationClipEditor(EditorContext& context, runtime::IApplicationHost& host,
                                     ui::runtime::UIHost& uiHost);
}
