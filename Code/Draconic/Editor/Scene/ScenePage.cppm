// Draconic::EditorScene - :page partition.
//
// SceneEditorPage: the scene document editor (design doc §3.6, phase 2). Each page owns its OWN
// live Scene (multi-scene rule - several pages open at once; everything scene-scoped is
// per-page): a ViewportView renders the scene through the REAL renderer via CameraOverride into
// the viewport's offscreen color target, an EditorCamera flies on the viewport's gated devices
// (hover/focus-gated, so occluded/inactive-tab input can't leak), and open/save round-trip the
// content DB through LoadScene/SaveScene. An empty scene shows a debug-draw ground grid + origin
// axes so navigation reads immediately.
//
// RegisterSceneEditor is the module's RegisterEditor entry point (§3.1): the EXECUTABLE calls it
// (editor core/app never link this module); it registers the SceneDocument type, the page
// factory, and the "Scene" asset creator (File > New Scene).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.scene:page;

import draconic.core;
import draconic.content;
import draconic.rhi;
import draconic.graphics;
import draconic.shell;
import draconic.runtime;
import draconic.runtime.client;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.scene.resource;
import draconic.scene.editor;
import draconic.render;
import draconic.render.subsystem;
import draconic.ui;
import draconic.ui.runtime;
import draconic.ui.viewport;
import draconic.vg.renderer;
import draconic.editor.core;
import draconic.editor.app;
import :camera;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace rt = draconic::runtime;
    namespace uirt = draconic::ui::runtime;
    namespace uivp = draconic::ui::viewport;
    namespace vgr = draconic::vg::renderer;
    namespace dscene = draconic::scene;
    namespace drender = draconic::render;

    /// ONE scene-renderer frame bracket for the whole editor: all scene pages' views render
    /// between a single BeginRendering/EndRendering per frame (the renderer's multi-view
    /// contract - a second bracket in the same frame resets per-frame transient pools and graph
    /// resources that the first bracket's still-unsubmitted commands reference, corrupting
    /// descriptor sets; and EndRendering clears EVERY scene's debug list, so later brackets lose
    /// their grids). Pages lazily open the bracket for their window on first render
    /// (EnsureOpen); EditorApplication's endSceneRendering hook closes it before the UI draws.
    ///
    /// LIMITATION: one bracket per frame means one WINDOW's scene pages render per frame. When
    /// pages live in a second OS window (floated page), those pages skip rendering (frozen
    /// image) until the renderer supports multi-encoder frames.
    class SceneRenderCoordinator
    {
    public:
        SceneRenderCoordinator() = default;
        SceneRenderCoordinator(const SceneRenderCoordinator&) = delete;
        SceneRenderCoordinator& operator=(const SceneRenderCoordinator&) = delete;

        /// Open the frame bracket for `frame`'s window (no-op if already open for it). False if
        /// rendering must be skipped: renderer not ready, or another window already holds this
        /// frame's bracket (the single-bracket limitation above).
        [[nodiscard]] bool EnsureOpen(rt::IApplicationHost& host, draconic::graphics::FrameContext& frame)
        {
            if (!frame.valid) { return false; }
            if (m_open) { return frame.window == m_window; }

            if (m_render == nullptr) { m_render = host.Ctx().GetSubsystem<drender::RenderSubsystem>(); }
            if (m_render == nullptr || !m_render->IsReady()) { return false; }

            // One bracket per host tick: windows rendered in the same tick share a frameIndex
            // (the device ring advances once per tick), so a same-index open attempt is a second
            // window this frame.
            if (m_hasLastFrame && frame.frameIndex == m_lastFrameIndex)
            {
                if (!m_warnedMultiWindow)
                {
                    m_warnedMultiWindow = true;
                    DRACONIC_LOG_WARNING(u8"Editor",
                        u8"scene viewports in a second window don't render yet (one render bracket per frame)");
                }
                return false;
            }

            m_render->BeginRendering(*frame.encoder, frame.frameIndex);
            m_open = true;
            m_window = frame.window;
            m_lastFrameIndex = frame.frameIndex;
            m_hasLastFrame = true;
            return true;
        }

        /// Close the bracket if it is open for `frame`'s window. Call after all pages rendered,
        /// before the UI draws (it samples the viewport targets the graph just wrote).
        void EndWindow(draconic::graphics::FrameContext& frame)
        {
            if (m_open && frame.window == m_window)
            {
                m_render->EndRendering();
                m_open = false;
                m_window = nullptr;
            }
        }

        [[nodiscard]] bool IsOpen() const noexcept { return m_open; }

    private:
        drender::RenderSubsystem* m_render = nullptr;   // borrowed (context subsystem)
        draconic::graphics::RenderWindow* m_window = nullptr;
        u32 m_lastFrameIndex = 0;
        bool m_hasLastFrame = false;
        bool m_open = false;
        bool m_warnedMultiWindow = false;
    };

    class SceneEditorPage final : public app::UIEditorPage
    {
    public:
        SceneEditorPage(EditorContext& context, rt::IApplicationHost& host, uirt::UIHost& uiHost,
                        SceneRenderCoordinator& coordinator, draconic::content::Instance& instance)
            : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_coordinator(&coordinator)
            , m_title(instance.Name())
        {
            m_scenes = host.Ctx().GetSubsystem<dscene::SceneSubsystem>();
            m_render = host.Ctx().GetSubsystem<drender::RenderSubsystem>();

            // Own live Scene per page (managers injected by the subsystems on CreateScene).
            if (m_scenes != nullptr)
            {
                m_scene = m_scenes->CreateScene(instance.Name());
                const Status loaded = dscene::LoadScene(instance, *m_scene);
                if (loaded.IsOk())
                {
                    DRACONIC_LOG_INFO(u8"Editor", u8"opened scene '{}'", m_title);
                }
                else if (loaded.Code() == ErrorCode::NotFound)
                {
                    DRACONIC_LOG_INFO(u8"Editor", u8"new scene '{}' (no scene stream yet)", m_title);
                }
                else
                {
                    DRACONIC_LOG_ERROR(u8"Editor", u8"scene '{}' failed to load", m_title);
                }
            }

            // No OnRender/RenderContent: the frame graph renders the scene into the color target
            // and manages its transitions via TargetState (ColorState()/SetColorState tracking),
            // inside the coordinator's single per-frame bracket.
            m_viewport = MakeRef<uivp::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.10f, 0.11f, 0.13f, 1.0f };

            m_router = MakeUnique<draconic::shell::InputRouter>(DefaultAllocator(), host.Shell()->Input());

            // Start framed on the origin (grid center), orbit pivot there, horizon level.
            m_camera.LookAt(Float3{ 0.0f, 0.0f, 0.0f });
        }

        // === UIEditorPage ===

        [[nodiscard]] draconic::ui::View* ContentView() override { return m_viewport.Get(); }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }

        void OnUpdate(rt::IApplicationHost&, f32 dt) override
        {
            EnsureViewportBound();
            if (m_hostWindow == nullptr) { return; }

            m_viewport->SyncInputRegion();
            m_router->Update();
            if (m_viewport->IsHovered() || m_viewport->IsFocused())
            {
                m_camera.Update(m_viewport->Keyboard(), m_viewport->Mouse(), dt);
            }

            // Ground grid + origin axes (per-scene debug draw: shows only where THIS scene
            // renders). Lists clear in EndRendering, so re-accumulate every frame.
            if (m_render != nullptr && m_scene != nullptr)
            {
                drender::debug::DebugDraw& dd = m_render->DebugScene(*m_scene);
                dd.DrawGrid(Float3{ 0, 0, 0 }, 20.0f, 20, Color{ 0.35f, 0.35f, 0.38f, 1.0f });
                dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 1, 0, 0 }, Color{ 0.9f, 0.2f, 0.2f, 1.0f });
                dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 0, 1, 0 }, Color{ 0.2f, 0.9f, 0.2f, 1.0f });
                dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 0, 0, 1 }, Color{ 0.2f, 0.4f, 0.95f, 1.0f });
            }
        }

        void OnRenderWindow(rt::IApplicationHost& host, draconic::graphics::FrameContext& frame) override
        {
            if (!m_viewport->IsReady() || !frame.valid || frame.window != m_hostWindow) { return; }
            if (m_render == nullptr || !m_render->IsReady() || m_scene == nullptr) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0) { return; }

            // All pages render inside the coordinator's ONE frame bracket (false = second-window
            // limitation or renderer not ready; skip, the UI shows the last rendered image).
            if (!m_coordinator->EnsureOpen(host, frame)) { return; }

            if (!m_renderedOnce)
            {
                m_renderedOnce = true;
                DRACONIC_LOG_DEBUG(u8"Editor", u8"scene page '{}' first frame ({}x{})", m_title, w, h);
            }

            drender::ViewCamera camera;
            camera.view = Float4x4::LookAtRH(m_camera.position,
                                             m_camera.position + m_camera.Forward(), m_camera.Up());
            camera.projection = Float4x4::PerspectiveFovRH(
                1.0472f, static_cast<f32>(w) / static_cast<f32>(h), 0.1f, 1000.0f);
            camera.position = m_camera.position;
            camera.farZ = 1000.0f;

            drender::CameraOverride cameraOverride;
            cameraOverride.camera = camera;
            cameraOverride.clearColor = Color{ m_viewport->ClearColor.r, m_viewport->ClearColor.g,
                                               m_viewport->ClearColor.b, m_viewport->ClearColor.a };

            // The graph imports the color target and owns its transitions: from the viewport's
            // tracked state (Undefined right after create/resize) to ShaderRead for the UI's
            // sampling - the Sandbox offscreen pattern.
            drender::TargetState targetState;
            targetState.texture = m_viewport->ColorTexture();
            targetState.currentState = m_viewport->ColorState();
            targetState.finalState = rhi::ResourceState::ShaderRead;

            m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(), w, h,
                                  drender::ViewportRect{ 0, 0, w, h }, &cameraOverride, targetState);
            m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
        }

        [[nodiscard]] Status Save() override
        {
            if (m_scene == nullptr || m_context->Project() == nullptr) { return Status{ ErrorCode::NotFound }; }
            draconic::content::Instance* instance = m_context->Project()->SourceDb().GetInstance(InstanceId());
            if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }

            const Status saved = dscene::SaveScene(*m_scene, *instance);
            if (saved.IsOk())
            {
                ClearDirty();
                DRACONIC_LOG_INFO(u8"Editor", u8"saved scene '{}'", m_title);
            }
            return saved;
        }

        void OnClose() override
        {
            // GPU targets + external-texture registration go while device + VGRenderer live.
            m_viewport->Shutdown();
            if (m_scenes != nullptr && m_scene != nullptr)
            {
                m_scenes->DestroyScene(m_scene);
                m_scene = nullptr;
            }
        }

        [[nodiscard]] dscene::Scene* ScenePtr() const noexcept { return m_scene; }
        [[nodiscard]] EditorCamera& Camera() noexcept { return m_camera; }

    private:
        // Bind (and re-bind after dock/float moves) the viewport to the window that hosts it -
        // the UISandbox UpdateViewportHostWindow dance: RendererFor is only valid once the
        // window is attached, and a floated panel lives in a different OS window.
        void EnsureViewportBound()
        {
            draconic::ui::RootView* root = m_viewport->Root();
            if (root == nullptr) { return; }
            draconic::graphics::RenderWindow* window = m_uiHost->WindowForRoot(root);
            if (window == nullptr || window == m_hostWindow) { return; }

            vgr::VGRenderer* renderer = m_uiHost->RendererFor(window);
            if (renderer == nullptr) { return; }   // float's AttachWindow hasn't run yet

            if (m_hostWindow == nullptr)
            {
                m_viewport->Initialize(m_host->Graphics()->Raw(), renderer,
                                       m_host->Shell()->Input(), window->Window().Id());
                if (m_viewport->Surface() != nullptr) { m_router->AddSurface(m_viewport->Surface()); }
            }
            else
            {
                m_viewport->AttachToWindow(renderer, window->Window().Id());
            }
            m_hostWindow = window;
        }

        EditorContext* m_context;                    // borrowed
        rt::IApplicationHost* m_host;                // borrowed
        uirt::UIHost* m_uiHost;                      // borrowed
        SceneRenderCoordinator* m_coordinator;       // borrowed (exe owns it)
        dscene::SceneSubsystem* m_scenes = nullptr;  // borrowed (context subsystem)
        drender::RenderSubsystem* m_render = nullptr;

        String m_title;
        dscene::Scene* m_scene = nullptr;            // owned by the SceneSubsystem
        RefPtr<uivp::ViewportView> m_viewport;
        UniquePtr<draconic::shell::InputRouter> m_router;
        EditorCamera m_camera;
        draconic::graphics::RenderWindow* m_hostWindow = nullptr;   // borrowed; tracks dock/float moves
        bool m_renderedOnce = false;   // first-frame debug log
    };

    // === Factory + registration (the module's RegisterEditor entry point, §3.1) ===

    class SceneEditorPageFactory final : public IEditorPageFactory
    {
    public:
        SceneEditorPageFactory(rt::IApplicationHost& host, uirt::UIHost& uiHost,
                               SceneRenderCoordinator& coordinator)
            : m_host(&host), m_uiHost(&uiHost), m_coordinator(&coordinator) {}

        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &dscene::SceneDocument::StaticType();
        }

        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            return UniquePtr<EditorPage>(
                DefaultAllocator().New<SceneEditorPage>(context, *m_host, *m_uiHost, *m_coordinator, instance),
                DefaultAllocator());
        }

    private:
        rt::IApplicationHost* m_host;
        uirt::UIHost* m_uiHost;
        SceneRenderCoordinator* m_coordinator;
    };

    // Create a fresh scene instance in the project's source DB under "Scenes/", named uniquely
    // (Scene, Scene2, ...). Writes the SceneDocument primary so the instance materializes; the
    // page treats the missing "scene" stream as an empty scene.
    inline draconic::content::Instance* CreateSceneInstance(EditorContext& context)
    {
        EditorProject* project = context.Project();
        if (project == nullptr) { return nullptr; }

        draconic::content::Group* root = project->SourceDb().RootGroup();
        draconic::content::Group* scenes = root->GetGroup(u8"Scenes");
        if (scenes == nullptr) { scenes = root->CreateGroup(u8"Scenes"); }
        if (scenes == nullptr) { return nullptr; }

        String name(u8"Scene");
        for (i32 counter = 2; scenes->GetInstance(name.AsView()) != nullptr; ++counter)
        {
            name = String(u8"Scene");
            name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
            if (counter >= 10) { name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10))); }
        }

        draconic::content::Instance* instance =
            scenes->CreateInstance(name.AsView(), dscene::SceneDocument::StaticType());
        if (instance == nullptr) { return nullptr; }

        dscene::SceneDocument doc;
        doc.name = name;
        if (!instance->WriteObject(doc).IsOk()) { return nullptr; }
        return instance;
    }

    inline void RegisterSceneEditor(EditorContext& context, rt::IApplicationHost& host, uirt::UIHost& uiHost,
                                    SceneRenderCoordinator& coordinator)
    {
        GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
        RegisterSerializable<dscene::SceneDocument>();

        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<SceneEditorPageFactory>(host, uiHost, coordinator), DefaultAllocator()));

        EditorContext::AssetCreator creator;
        creator.label = String(u8"Scene");
        creator.create = [](EditorContext& ctx) { return CreateSceneInstance(ctx); };
        context.RegisterCreator(Move(creator));
    }
}
