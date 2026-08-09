// Draconic::EditorApp - :ui_page partition.
//
// UIEditorPage: the UI-side extension of the headless editor::EditorPage - a page that owns a
// foundation.ui content view (docked as a closable center tab by EditorApplication) and receives
// the app's frame hooks so it can drive per-page work (viewport binding, camera, offscreen
// rendering). Every IEditorPageFactory registered into THIS app's context must produce
// UIEditorPages (EditorApplication static_casts on open) - the headless EditorPage stays UI-free
// for core tests, this is the one seam where pages meet the UI/runtime.

module;
#include "Core/Prelude.h"

export module editor.app:ui_page;

import foundation.core;
import foundation.graphics;
import foundation.runtime.client;
import foundation.ui;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    class UIEditorPage : public editor::EditorPage
    {
    public:
        /// The view docked into the center document area (owned by the page).
        [[nodiscard]] virtual foundation::ui::View* ContentView() = 0;

        /// Per-frame hook, after the UI laid out (viewport rects are current).
        virtual void OnUpdate(foundation::runtime::IApplicationHost& host, f32 dt)
        {
            (void)host;
            (void)dt;
        }

        /// Per-window render hook, before the UI draws (offscreen content the UI then samples).
        virtual void OnRenderWindow(foundation::runtime::IApplicationHost& host,
                                    foundation::graphics::FrameContext& frame)
        {
            (void)host;
            (void)frame;
        }

        /// After the scene renderer's EndRendering (targets are COMPOSED): overlays that
        /// draw ON the page's offscreen content (the Game tab's screen-tier UI).
        virtual void OnAfterSceneRender(foundation::runtime::IApplicationHost& host,
                                        foundation::graphics::FrameContext& frame)
        {
            (void)host;
            (void)frame;
        }

        /// Called right before the page is removed - release GPU/scene resources while the
        /// device and window are still alive.
        virtual void OnClose() {}
    };
}
