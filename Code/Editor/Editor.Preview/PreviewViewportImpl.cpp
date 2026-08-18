// PreviewViewport implementation (see PreviewViewport.cppm). The bodies here are the
// substrate the bespoke pages used to each hand-roll verbatim: build a private preview
// scene, bind the ViewportView to its host window on first frame, drive the EditorCamera
// from the gated viewport devices, and render the scene through the real renderer with a
// CameraOverride into the viewport's color target.

module;
#include "Core/Prelude.h"

module editor.preview;

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
import editor.camera;

using namespace foundation::core;

namespace editor
{
    namespace rhi = foundation::rhi;
    namespace render = foundation::render;
    namespace vg = foundation::vg;

    PreviewViewport::PreviewViewport(runtime::IApplicationHost& host, ui::runtime::UIHost& uiHost,
                                     StringView sceneName)
        : m_host(&host), m_uiHost(&uiHost)
    {
        m_router =
            MakeUnique<foundation::shell::InputRouter>(DefaultAllocator(), host.Shell()->Input());

        m_scenes = host.Ctx().GetSubsystem<engine::scene::SceneSubsystem>();
        m_render = host.Ctx().GetSubsystem<engine::render::RenderSubsystem>();

        if (m_scenes != nullptr)
        {
            m_sceneManager.SetAwareRegistry(&m_scenes->AwareRegistry());
            m_scenes->RegisterManager(&m_sceneManager);
            m_scene = m_sceneManager.CreateScene(sceneName);
            m_scene->SetSimulationEnabled(false);
        }

        m_viewport = MakeRef<ui::viewport::ViewportView>(DefaultAllocator());
        m_viewport->ClearColor = rhi::ClearColor{0.10f, 0.11f, 0.13f, 1.0f};
    }

    PreviewViewport::~PreviewViewport()
    {
        Shutdown();
    }

    render::debug::DebugDraw& PreviewViewport::SceneDebugDraw()
    {
        return m_render->DebugScene(*m_scene);
    }

    void PreviewViewport::Update(f32 dt)
    {
        EnsureViewportBound();
        if (m_hostWindow == nullptr)
        {
            return;
        }
        m_viewport->SyncInputRegion();
        if (m_router)
        {
            m_router->Update();
        }
        if (m_viewport->IsHovered() || m_viewport->IsFocused())
        {
            m_camera.Update(m_viewport->Keyboard(), m_viewport->Mouse(), dt);
        }
    }

    void PreviewViewport::RenderFrame(foundation::graphics::FrameContext& frame)
    {
        if (m_viewport.Get() == nullptr || !m_viewport->IsReady() || !frame.valid)
        {
            return;
        }
        if (m_render == nullptr || !m_render->IsReady() || m_scene == nullptr)
        {
            return;
        }
        const u32 w = m_viewport->RenderWidth();
        const u32 h = m_viewport->RenderHeight();
        if (w == 0 || h == 0 || !m_viewport->IsEffectivelyVisible())
        {
            return;
        }

        render::ViewCamera camera;
        camera.view = Float4x4::LookAtRH(m_camera.position, m_camera.position + m_camera.Forward(),
                                         m_camera.Up());
        camera.projection = Float4x4::PerspectiveFovRH(
            1.0472f, static_cast<f32>(w) / static_cast<f32>(h), 0.05f, 500.0f);
        camera.position = m_camera.position;
        camera.farZ = 500.0f;

        render::CameraOverride cameraOverride;
        cameraOverride.camera = camera;
        cameraOverride.clearColor = Color{m_viewport->ClearColor.r, m_viewport->ClearColor.g,
                                          m_viewport->ClearColor.b, m_viewport->ClearColor.a};

        render::TargetState targetState;
        targetState.texture = m_viewport->ColorTexture();
        targetState.currentState = m_viewport->ColorState();
        targetState.finalState = rhi::ResourceState::ShaderRead;

        m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(), w,
                              h, render::ViewportRect{0, 0, w, h}, &cameraOverride, targetState);
        m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
    }

    void PreviewViewport::Shutdown()
    {
        if (m_viewport.Get() != nullptr)
        {
            m_viewport->Shutdown();
        }
        if (m_scene != nullptr)
        {
            m_sceneManager.DestroyScene(m_scene);
            m_scene = nullptr;
        }
        if (m_scenes != nullptr)
        {
            m_scenes->UnregisterManager(&m_sceneManager);
            m_scenes = nullptr;
        }
    }

    void PreviewViewport::EnsureViewportBound()
    {
        foundation::ui::RootView* root = m_viewport->Root();
        if (root == nullptr)
        {
            return;
        }
        foundation::graphics::RenderWindow* window = m_uiHost->WindowForRoot(root);
        if (window == nullptr || window == m_hostWindow)
        {
            return;
        }
        vg::renderer::VGRenderer* renderer = m_uiHost->RendererFor(window);
        if (renderer == nullptr)
        {
            return;
        }
        if (m_hostWindow == nullptr)
        {
            m_viewport->Initialize(m_host->Graphics()->Raw(), renderer, m_host->Shell()->Input(),
                                   window->Window().Id());
            if (m_viewport->Surface() != nullptr)
            {
                m_router->AddSurface(m_viewport->Surface());
            }
        }
        else
        {
            m_viewport->AttachToWindow(renderer, window->Window().Id());
        }
        m_hostWindow = window;
    }
}
