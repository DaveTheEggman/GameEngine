// Editor::Preview - `editor.preview`: the shared 3D-preview substrate for bespoke asset
// editor pages (mesh / clip / skeleton / material / particle / animgraph / collision).
//
// PreviewViewport owns the whole substrate that those pages used to each hand-roll: a
// ViewportView, a private preview Scene (simulation off) in its own SceneManager, an
// EditorCamera, the InputRouter, and the EnsureViewportBound + camera-update + RenderScene
// loop. A page CONTAINS one instead of re-implementing it: construct it, populate Scene()
// with the page's own entities (mesh, light, ...), drive Update(dt) from OnUpdate and
// RenderFrame(frame) from OnRenderWindow, mount View() in the layout, and draw overlays
// into SceneDebugDraw() (which is render->DebugScene(Scene()) - the keyed-view contract:
// a preview scene has exactly one view, so DebugScene is unambiguous here).
//
// Never linked by the runtime. See Documentation/Specs/editor-preview-viewport.md.

module;
#include "Core/Prelude.h"

export module editor.preview;

import foundation.core;
import foundation.shell;
import foundation.runtime;
import foundation.runtime.client;
import foundation.graphics;
import foundation.rhi;
import foundation.scene;
import engine.scene;
import foundation.render;
import engine.render;
import foundation.ui;
import foundation.ui.runtime;
import foundation.ui.viewport;
import foundation.vg.renderer;
export import editor.camera; // EditorCamera (lean module; kept out of this heavy interface)

using namespace foundation::core;

export namespace editor
{
    namespace runtime = foundation::runtime;
    namespace ui = foundation::ui;
    namespace scene = foundation::scene;

    // Shared 3D-preview viewport substrate. Non-copyable, non-movable (owns a SceneManager
    // by value + a live ViewportView); hold it by UniquePtr in the page.
    class PreviewViewport
    {
    public:
        // Builds the viewport + a private preview scene named `sceneName` (e.g. "mesh.preview").
        // The scene has simulation disabled; the page adds its own entities via Scene().
        PreviewViewport(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost,
                        StringView sceneName);
        ~PreviewViewport();

        PreviewViewport(const PreviewViewport&) = delete;
        PreviewViewport& operator=(const PreviewViewport&) = delete;

        [[nodiscard]] bool IsValid() const { return m_scene != nullptr; }
        [[nodiscard]] scene::Scene* Scene() const { return m_scene; }
        [[nodiscard]] ui::viewport::ViewportView* View() const { return m_viewport.Get(); }
        [[nodiscard]] EditorCamera& Camera() { return m_camera; }

        // Overlay draw target for this preview scene (skeleton wireframe, collision outline, ...).
        // Valid only while IsValid(); the render subsystem must be present.
        [[nodiscard]] foundation::render::debug::DebugDraw& SceneDebugDraw();

        // Per-frame input + camera drive. Call from the page's OnUpdate BEFORE any page logic
        // that reads the camera. Safe before the viewport is bound (no-ops until then).
        void Update(f32 dt);

        // Render the preview scene into the viewport. Call from the page's OnRenderWindow.
        void RenderFrame(foundation::graphics::FrameContext& frame);

        // Tear down the viewport + destroy the preview scene. Call from the page's OnClose.
        void Shutdown();

    private:
        void EnsureViewportBound();

        runtime::IApplicationHost* m_host = nullptr;
        ui::runtime::UIHost* m_uiHost = nullptr;
        engine::scene::SceneSubsystem* m_scenes = nullptr;
        scene::SceneManager m_sceneManager; // this preview's OWN scene group
        engine::render::RenderSubsystem* m_render = nullptr;
        scene::Scene* m_scene = nullptr;
        EditorCamera m_camera;
        UniquePtr<foundation::shell::InputRouter> m_router;
        RefPtr<ui::viewport::ViewportView> m_viewport;
        foundation::graphics::RenderWindow* m_hostWindow = nullptr;
    };
}
