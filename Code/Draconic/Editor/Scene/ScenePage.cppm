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
import draconic.ui.toolkit;
import draconic.ui.runtime;
import draconic.ui.viewport;
import draconic.vg.renderer;
import draconic.editor.core;
import draconic.editor.app;
import :camera;
import :edit;
import :gizmo;
import :component_gizmos;
import :hierarchy;
import :inspector;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace rt = draconic::runtime;
    namespace uirt = draconic::ui::runtime;
    namespace uivp = draconic::ui::viewport;
    namespace vgr = draconic::vg::renderer;
    namespace dscene = draconic::scene;
    namespace drender = draconic::render;

    class SceneEditorPage final : public app::UIEditorPage
    {
    public:
        SceneEditorPage(EditorContext& context, rt::IApplicationHost& host, uirt::UIHost& uiHost,
                        draconic::content::Instance& instance)
            : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_title(instance.Name())
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
                    // Bind the scene's resource refs to cooked products (no-op refs stay null;
                    // a later cook + reopen picks them up - live hot reload is the 6d pass).
                    if (context.Resources() != nullptr)
                    {
                        dscene::ResolveSceneResources(*m_scene, *context.Resources());
                    }
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
            // inside the app's single per-frame bracket.
            m_viewport = MakeRef<uivp::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.10f, 0.11f, 0.13f, 1.0f };

            // Everything scene-scoped is PER PAGE (multi-scene): mutation mediator (all edits
            // are commands on THIS page's stack), selection, hierarchy + inspector views.
            if (m_scene != nullptr)
            {
                m_editContext = MakeUnique<SceneEditContext>(DefaultAllocator(), *m_scene, Commands());
                m_hierarchy = MakeRef<SceneHierarchyView>(DefaultAllocator(), *m_editContext);
                m_inspector = MakeRef<SceneInspectorView>(DefaultAllocator(), context, *m_editContext);
                m_gizmos = MakeUnique<GizmoController>(DefaultAllocator(), *m_editContext);
                RegisterBuiltinGizmoRenderers(m_componentGizmos);
            }

            // Page layout: hierarchy | (viewport | inspector).
            auto inner = MakeRef<draconic::ui::toolkit::SplitView>(DefaultAllocator());
            inner->SetSplitRatio(0.72f);
            inner->SetPanes(m_viewport.Get(), m_inspector.Get());
            m_content = MakeRef<draconic::ui::toolkit::SplitView>(DefaultAllocator());
            m_content->SetSplitRatio(0.2f);
            m_content->SetPanes(m_hierarchy.Get(), inner.Get());

            m_router = MakeUnique<draconic::shell::InputRouter>(DefaultAllocator(), host.Shell()->Input());

            // Start framed on the origin (grid center), orbit pivot there, horizon level.
            m_camera.LookAt(Float3{ 0.0f, 0.0f, 0.0f });
        }

        // === UIEditorPage ===

        [[nodiscard]] draconic::ui::View* ContentView() override { return m_content.Get(); }
        [[nodiscard]] StringView Title() const override { return m_title.AsView(); }

        void OnUpdate(rt::IApplicationHost&, f32 dt) override
        {
            EnsureViewportBound();
            if (m_hostWindow == nullptr) { return; }

            if (m_hierarchy) { m_hierarchy->Refresh(); }   // scene revision -> tree rebuild
            if (m_inspector) { m_inspector->Refresh(); }   // selection/structure -> grid rebuild

            m_viewport->SyncInputRegion();
            m_router->Update();
            const bool viewportActive = m_viewport->IsHovered() || m_viewport->IsFocused();
            if (viewportActive)
            {
                m_camera.Update(m_viewport->Keyboard(), m_viewport->Mouse(), dt);
            }
            const bool gizmoConsumedMouse = UpdateGizmos(viewportActive);
            if (m_viewport->IsHovered() && !gizmoConsumedMouse) { PickOnClick(); }

            // Per-scene debug draw (shows only where THIS scene renders; lists clear in
            // EndRendering, so re-accumulate every frame): ground grid + origin axes + entity
            // markers (selected = boxed and brighter) + gizmos.
            if (m_render != nullptr && m_scene != nullptr)
            {
                drender::debug::DebugDraw& dd = m_render->DebugScene(*m_scene);
                dd.DrawGrid(Float3{ 0, 0, 0 }, 20.0f, 20, Color{ 0.35f, 0.35f, 0.38f, 1.0f });
                dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 1, 0, 0 }, Color{ 0.9f, 0.2f, 0.2f, 1.0f });
                dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 0, 1, 0 }, Color{ 0.2f, 0.9f, 0.2f, 1.0f });
                dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 0, 0, 1 }, Color{ 0.2f, 0.4f, 0.95f, 1.0f });
                DrawEntityMarkers(dd);
                DrawGizmos(dd);
            }
        }

        // Called only for the MAIN window's frame, inside the app-level scene-renderer bracket
        // (Sedulous structure): the offscreen target is window-agnostic, so this renders no
        // matter which OS window hosts the panel; that window's UI samples the result.
        void OnRenderWindow(rt::IApplicationHost&, draconic::graphics::FrameContext& frame) override
        {
            if (!m_viewport->IsReady() || !frame.valid) { return; }
            if (m_render == nullptr || !m_render->IsReady() || m_scene == nullptr) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0) { return; }

            // Skip hidden viewports (inactive dock tabs set Visibility=Gone up the ancestor
            // chain) - no GPU work for content nobody can see (Sedulous does the same).
            if (!m_viewport->IsEffectivelyVisible()) { return; }

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
        [[nodiscard]] SceneEditContext* EditContext() const noexcept { return m_editContext.Get(); }

    private:
        static constexpr f32 kFovY = 1.0472f;   // must match OnRenderWindow's projection

        // Camera ray through the mouse position, built from the camera basis (no matrix inverse).
        [[nodiscard]] bool MakeMouseRay(GizmoRay& out) const
        {
            draconic::shell::IMouse* mouse = m_viewport->Mouse();
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (mouse == nullptr || w == 0 || h == 0) { return false; }
            const f32 ndcX = 2.0f * (mouse->X() / static_cast<f32>(w)) - 1.0f;
            const f32 ndcY = 1.0f - 2.0f * (mouse->Y() / static_cast<f32>(h));
            const f32 tanY = Tan(kFovY * 0.5f);
            const f32 tanX = tanY * (static_cast<f32>(w) / static_cast<f32>(h));
            out.origin = m_camera.position;
            out.direction = Normalized(m_camera.Forward()
                                     + m_camera.Right() * (ndcX * tanX)
                                     + m_camera.Up() * (ndcY * tanY));
            return true;
        }

        // Feed the gizmo controller a frame of viewport input. Runs EVERY frame so the gizmo
        // pose tracks tree selections and undo/redo even while the mouse is elsewhere; when the
        // viewport isn't hovered/focused the pointer is flagged invalid (pose-sync only). The
        // camera owns the mouse while Alt (orbit) or RMB (fly) is down, so gizmo buttons are
        // masked then. Returns true when the gizmo consumed the mouse (hot handle or active
        // drag) - click-picking must skip.
        [[nodiscard]] bool UpdateGizmos(bool viewportActive)
        {
            if (!m_gizmos) { return false; }
            GizmoFrameInput in;
            in.cameraPosition = m_camera.position;
            in.cameraForward = m_camera.Forward();
            in.fovY = kFovY;
            in.pointerValid = viewportActive && MakeMouseRay(in.ray);
            if (!in.pointerValid) { return m_gizmos->Update(in); }

            draconic::shell::IMouse* mouse = m_viewport->Mouse();
            draconic::shell::IKeyboard* kb = m_viewport->Keyboard();

            const bool cameraOwnsMouse =
                (kb != nullptr && (kb->IsKeyDown(draconic::shell::KeyCode::LeftAlt)
                                || kb->IsKeyDown(draconic::shell::KeyCode::RightAlt)))
                || mouse->IsButtonDown(draconic::shell::MouseButton::Right)
                || m_camera.mouseCaptured;   // Tab-captured fly mode owns WASD too
            if (!cameraOwnsMouse)
            {
                in.leftPressed = mouse->IsButtonPressed(draconic::shell::MouseButton::Left);
                in.leftDown = mouse->IsButtonDown(draconic::shell::MouseButton::Left);
            }
            // Release always reaches the controller so an in-flight drag can finish even if a
            // modifier goes down mid-drag.
            in.leftReleased = mouse->IsButtonReleased(draconic::shell::MouseButton::Left)
                           || !mouse->IsButtonDown(draconic::shell::MouseButton::Left);
            if (kb != nullptr)
            {
                in.snap = kb->IsKeyDown(draconic::shell::KeyCode::LeftCtrl)
                       || kb->IsKeyDown(draconic::shell::KeyCode::RightCtrl);
                if (!cameraOwnsMouse)   // W/E/R fly keys belong to the camera while flying
                {
                    in.keyTranslate = kb->IsKeyPressed(draconic::shell::KeyCode::W);
                    in.keyRotate = kb->IsKeyPressed(draconic::shell::KeyCode::E);
                    in.keyScale = kb->IsKeyPressed(draconic::shell::KeyCode::R);
                    in.keyToggleSpace = kb->IsKeyPressed(draconic::shell::KeyCode::X);
                }
            }
            return m_gizmos->Update(in);
        }

        void DrawGizmos(drender::debug::DebugDraw& dd)
        {
            if (m_gizmos)
            {
                m_gizmos->Draw(dd);
                if (m_gizmos->IsActive())
                {
                    dd.DrawScreenText(12.0f, 12.0f, m_gizmos->StatusText(),
                                      Color{ 0.85f, 0.85f, 0.85f, 1.0f });
                }
            }

            // Component gizmos: full set for the selected entity; opted-in renderers for the rest.
            if (!m_editContext) { return; }
            GizmoContext ctx;
            ctx.debug = &dd;
            ctx.scene = m_scene;
            ctx.cameraPosition = m_camera.position;
            Selection<Guid>& selection = m_editContext->EntitySelection();
            m_scene->ForEachEntity([&](dscene::EntityHandle e) {
                m_componentGizmos.DrawEntity(e, selection.Contains(m_scene->GetEntityId(e)), ctx);
            });
        }

        // Position markers for every entity (small cross; selected = brighter + boxed) - empty
        // entities have no renderable, so the editor gives them a visual anchor.
        void DrawEntityMarkers(drender::debug::DebugDraw& dd)
        {
            if (!m_editContext) { return; }
            Selection<Guid>& selection = m_editContext->EntitySelection();
            dscene::Scene& scene = *m_scene;
            scene.ForEachEntity([&](dscene::EntityHandle e) {
                const Float4x4 world = scene.GetWorldMatrix(e);
                const Float3 p{ world.m[3][0], world.m[3][1], world.m[3][2] };
                const bool selected = selection.Contains(scene.GetEntityId(e));
                const f32 s = 0.25f;
                const Color color = selected ? Color{ 1.0f, 0.85f, 0.25f, 1.0f }
                                             : Color{ 0.75f, 0.75f, 0.80f, 1.0f };
                dd.DrawLine(p - Float3{ s, 0, 0 }, p + Float3{ s, 0, 0 }, color);
                dd.DrawLine(p - Float3{ 0, s, 0 }, p + Float3{ 0, s, 0 }, color);
                dd.DrawLine(p - Float3{ 0, 0, s }, p + Float3{ 0, 0, s }, color);
                if (selected)
                {
                    dd.DrawWireBoxCenter(p, Float3{ 0.35f, 0.35f, 0.35f }, color);
                }
            });
        }

        // Click-to-select in the viewport: a camera ray through the clicked pixel against small
        // pick spheres at entity positions (CPU picking v1; component bounds and marquee later).
        // Left click only, and not while Alt-orbiting; Ctrl toggles; empty space clears.
        void PickOnClick()
        {
            if (!m_editContext) { return; }
            draconic::shell::IMouse* mouse = m_viewport->Mouse();
            draconic::shell::IKeyboard* kb = m_viewport->Keyboard();
            if (mouse == nullptr || !mouse->IsButtonPressed(draconic::shell::MouseButton::Left)) { return; }
            const bool alt = kb != nullptr && (kb->IsKeyDown(draconic::shell::KeyCode::LeftAlt)
                                            || kb->IsKeyDown(draconic::shell::KeyCode::RightAlt));
            if (alt) { return; }   // Alt+LMB = camera orbit

            GizmoRay pickRay;
            if (!MakeMouseRay(pickRay)) { return; }
            const Float3 origin = pickRay.origin;
            const Float3 dir = pickRay.direction;

            dscene::Scene& scene = *m_scene;
            Guid best;
            f32 bestT = kFloatMax;
            scene.ForEachEntity([&](dscene::EntityHandle e) {
                const Float4x4 world = scene.GetWorldMatrix(e);
                const Float3 p{ world.m[3][0], world.m[3][1], world.m[3][2] };
                const Float3 toCenter = p - origin;
                const f32 t = Dot(toCenter, dir);
                if (t <= 0.0f || t >= bestT) { return; }
                const Float3 closest = origin + dir * t;
                const Float3 d = p - closest;
                // Screen-constant-ish pick radius: grows with distance, floors for close-ups.
                const f32 radius = Max(0.15f, t * 0.02f);
                if (Dot(d, d) <= radius * radius)
                {
                    bestT = t;
                    best = scene.GetEntityId(e);
                }
            });

            Selection<Guid>& selection = m_editContext->EntitySelection();
            const bool ctrl = kb != nullptr && (kb->IsKeyDown(draconic::shell::KeyCode::LeftCtrl)
                                             || kb->IsKeyDown(draconic::shell::KeyCode::RightCtrl));
            if (best != Guid{})
            {
                if (ctrl) { selection.Toggle(best); }
                else { selection.Set(best); }
            }
            else if (!ctrl)
            {
                selection.Clear();
            }
        }

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
        dscene::SceneSubsystem* m_scenes = nullptr;  // borrowed (context subsystem)
        drender::RenderSubsystem* m_render = nullptr;

        String m_title;
        dscene::Scene* m_scene = nullptr;            // owned by the SceneSubsystem
        UniquePtr<SceneEditContext> m_editContext;   // per-page mutation mediator + selection
        RefPtr<draconic::ui::toolkit::SplitView> m_content;   // hierarchy | viewport
        RefPtr<SceneHierarchyView> m_hierarchy;
        RefPtr<SceneInspectorView> m_inspector;
        UniquePtr<GizmoController> m_gizmos;
        GizmoRendererRegistry m_componentGizmos;
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
        SceneEditorPageFactory(rt::IApplicationHost& host, uirt::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost) {}

        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &dscene::SceneDocument::StaticType();
        }

        [[nodiscard]] UniquePtr<EditorPage> CreatePage(EditorContext& context,
                                                       draconic::content::Instance& instance) override
        {
            return UniquePtr<EditorPage>(
                DefaultAllocator().New<SceneEditorPage>(context, *m_host, *m_uiHost, instance),
                DefaultAllocator());
        }

    private:
        rt::IApplicationHost* m_host;
        uirt::UIHost* m_uiHost;
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

    inline void RegisterSceneEditor(EditorContext& context, rt::IApplicationHost& host, uirt::UIHost& uiHost)
    {
        GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
        RegisterSerializable<dscene::SceneDocument>();

        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<SceneEditorPageFactory>(host, uiHost), DefaultAllocator()));

        EditorContext::AssetCreator creator;
        creator.label = String(u8"Scene");
        creator.create = [](EditorContext& ctx) { return CreateSceneInstance(ctx); };
        context.RegisterCreator(Move(creator));
    }
}
