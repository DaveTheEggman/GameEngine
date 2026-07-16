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
                m_scene->SetSimulationEnabled(false);   // edit mode is frozen; Simulate un-freezes
                const Status loaded = dscene::LoadScene(instance, *m_scene);
                if (loaded.IsOk())
                {
                    // Bind the scene's resource refs to cooked products (no-op refs stay null;
                    // a later cook + reopen picks them up - live hot reload is the 6d pass).
                    if (context.Resources() != nullptr)
                    {
                        dscene::ResolveSceneResources(*m_scene, *context.Resources());
                    }
                    // Prefab instances load as ref+deltas - respawn them from the SOURCE DB
                    // (payloads are edited assets, not cooked products), then bind the
                    // spawned components' refs too.
                    if (m_scene->PendingPrefabInstanceCount() > 0 && context.Project() != nullptr)
                    {
                        EditorContext* editorContext = &context;
                        dscene::ResolveScenePrefabs(*m_scene,
                            Function<UniquePtr<IStream>(const Guid&)>{
                                [editorContext](const Guid& prefabId) -> UniquePtr<IStream> {
                                    draconic::content::Instance* prefab =
                                        editorContext->Project()->SourceDb().GetInstance(prefabId);
                                    return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                               : UniquePtr<IStream>{};
                                } });
                        if (context.Resources() != nullptr)
                        {
                            dscene::ResolveSceneResources(*m_scene, *context.Resources());
                        }
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
                m_editContext->SetResources(context.Resources());
                m_hierarchy = MakeRef<SceneHierarchyView>(DefaultAllocator(), *m_editContext);
                m_hierarchy->SetEditorContext(&context);
                {
                    SceneEditorPage* page = this;
                    m_hierarchy->OnCreatePrefab = [page](const Guid& entity) { page->CreatePrefabFromEntity(entity); };
                    m_hierarchy->OnSpawnPrefab = [page](const Guid& parent) { page->PickAndSpawnPrefab(parent); };
                    m_hierarchy->OnApplyPrefab = [page](const Guid& root) { page->ApplyInstanceToPrefab(root); };
                    m_hierarchy->OnRevertPrefab = [page](const Guid& root) { page->RevertInstance(root); };
                }
                m_inspector = MakeRef<SceneInspectorView>(DefaultAllocator(), context, *m_editContext);
                m_gizmos = MakeUnique<GizmoController>(DefaultAllocator(), *m_editContext);
                RegisterBuiltinGizmoRenderers(m_componentGizmos);
            }

            // Viewport pane: [toolbar strip | viewport]. The toolbar mirrors and drives the
            // gizmo state the W/E/R/X keys already control - the on-screen answer to "which
            // space am I in" (the recorded gap: X toggled with no visible state anywhere).
            BuildViewportToolbar();
            auto viewportPane = MakeRef<draconic::ui::FlexLayout>(DefaultAllocator());
            viewportPane->Direction = draconic::ui::Orientation::Vertical;
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                lp->Height = draconic::ui::SizeSpec::Fixed(draconic::ui::Unit::Px(30));
                viewportPane->AddView(m_toolbar.Get(), lp);
            }
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                viewportPane->AddView(m_viewport.Get(), lp);
            }

            // Page layout: hierarchy | (viewport | inspector).
            auto inner = MakeRef<draconic::ui::toolkit::SplitView>(DefaultAllocator());
            inner->SetSplitRatio(0.72f);
            inner->SetPanes(viewportPane.Get(), m_inspector.Get());
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
                if (m_showGrid)
                {
                    dd.DrawGrid(Float3{ 0, 0, 0 }, 20.0f, 20, Color{ 0.35f, 0.35f, 0.38f, 1.0f });
                    dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 1, 0, 0 }, Color{ 0.9f, 0.2f, 0.2f, 1.0f });
                    dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 0, 1, 0 }, Color{ 0.2f, 0.9f, 0.2f, 1.0f });
                    dd.DrawLine(Float3{ 0, 0, 0 }, Float3{ 0, 0, 1 }, Color{ 0.2f, 0.4f, 0.95f, 1.0f });
                }
                DrawEntityMarkers(dd);
                DrawGizmos(dd);
            }
            SyncToolbar();
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

        // Create-from-selection: capture the subtree as a prefab asset (under "Prefabs/",
        // named after the entity) and replace the original with an instance of it (one undo
        // group). The payload keeps the captured guids as its stable source ids.
        void CreatePrefabFromEntity(const Guid& entityId)
        {
            if (m_scene == nullptr || m_context->Project() == nullptr) { return; }
            const dscene::EntityHandle live = m_editContext->Resolve(entityId);
            if (!live.IsAssigned()) { return; }

            MemoryStream payload;
            if (!dscene::CapturePrefab(*m_scene, live, payload).IsOk())
            {
                m_context->Notify(draconic::editor::NoticeKind::Error, u8"Prefab capture failed.");
                return;
            }

            draconic::content::Group* root = m_context->Project()->SourceDb().RootGroup();
            draconic::content::Group* prefabs = root->GetGroup(u8"Prefabs");
            if (prefabs == nullptr) { prefabs = root->CreateGroup(u8"Prefabs"); }
            String name(m_scene->GetEntityName(live));
            if (name.IsEmpty()) { name = String(u8"Prefab"); }
            for (u32 n = 2; prefabs->GetInstance(name.AsView()) != nullptr; ++n)
            {
                name = String(m_scene->GetEntityName(live));
                name += u8".";
                utf8char digits[12];
                u32 value = n, count = 0;
                do { digits[count++] = static_cast<utf8char>('0' + (value % 10)); value /= 10; } while (value != 0);
                while (count > 0) { name.PushBack(digits[--count]); }
            }
            draconic::content::Instance* asset =
                prefabs->CreateInstance(name.AsView(), dscene::PrefabDocument::StaticType());
            if (asset == nullptr) { return; }
            dscene::PrefabDocument doc;
            doc.name = name;
            if (!asset->WriteObject(doc).IsOk()
                || !asset->WriteData(u8"scene", payload.Bytes()).IsOk())
            {
                m_context->Notify(draconic::editor::NoticeKind::Error, u8"Prefab asset write failed.");
                return;
            }

            Array<byte> bytes;
            const Span<const byte> view = payload.Bytes();
            bytes.Reserve(view.Size());
            for (byte b : view) { bytes.PushBack(b); }
            const Guid instanceRoot =
                m_editContext->ReplaceWithPrefabInstance(entityId, asset->Id(), Move(bytes));
            if (!instanceRoot.IsNil())
            {
                String message(u8"Created prefab '");
                message += name;
                message += u8"'.";
                m_context->Notify(draconic::editor::NoticeKind::Success, message.AsView());
            }
        }

        // Apply-to-prefab entry point: rewrites the asset and rebuilds every instance, with
        // no undo - so it confirms first (mirror of RevertInstance).
        void ApplyInstanceToPrefab(const Guid& rootId)
        {
            if (m_scene == nullptr || m_context->Project() == nullptr
                || m_content->Context == nullptr)
            {
                return;
            }
            dscene::Scene::PrefabInstanceState* state = m_scene->FindPrefabInstanceByRoot(rootId);
            if (state == nullptr) { return; }
            draconic::content::Instance* asset =
                m_context->Project()->SourceDb().GetInstance(state->prefabId);
            if (asset == nullptr)
            {
                m_context->Notify(draconic::editor::NoticeKind::Warning,
                                  u8"The instance's prefab asset no longer exists.");
                return;
            }
            dscene::EntityHandle root = m_scene->FindEntity(rootId);
            String message(u8"Apply '");
            message += m_scene->GetEntityName(root);
            message += u8"' to prefab '";
            message += asset->Name();
            message += u8"'? The prefab asset is rewritten and every instance in open scenes "
                       u8"updates to match. This cannot be undone.";
            SceneEditorPage* page = this;
            RefPtr<draconic::ui::Dialog> dialog =
                draconic::ui::Dialog::Confirm(u8"Apply to Prefab", message.AsView());
            dialog->OnClosed.Add(
                draconic::ui::Event<void(draconic::ui::Dialog*, draconic::ui::DialogResult)>::Handler{
                    [page, rootId](draconic::ui::Dialog*, draconic::ui::DialogResult result) {
                        if (result != draconic::ui::DialogResult::OK) { return; }
                        // Deferred: rebuilding instances destroys + respawns entities (and
                        // their hierarchy rows), never mid-event-dispatch.
                        draconic::ui::UIContext* ctx = page->m_content->Context;
                        if (ctx == nullptr) { return; }
                        ctx->MutationQueueRef().QueueAction(Function<void()>{
                            [page, rootId]() { page->ApplyInstanceToPrefabNow(rootId); } });
                    } });
            dialog->Show(m_content->Context);
        }

        // Apply-to-prefab: the instance's CURRENT state becomes the template (source-id
        // keyed, so other instances' deltas stay valid), then every instance everywhere
        // rebuilds from it - including this one, which re-baselines to clean.
        void ApplyInstanceToPrefabNow(const Guid& rootId)
        {
            if (m_scene == nullptr || m_context->Project() == nullptr) { return; }
            dscene::Scene::PrefabInstanceState* state = m_scene->FindPrefabInstanceByRoot(rootId);
            if (state == nullptr) { return; }
            draconic::content::Instance* asset =
                m_context->Project()->SourceDb().GetInstance(state->prefabId);
            if (asset == nullptr)
            {
                m_context->Notify(draconic::editor::NoticeKind::Warning,
                                  u8"The instance's prefab asset no longer exists.");
                return;
            }
            MemoryStream payload;
            if (!dscene::CaptureInstanceAsTemplate(*m_scene, *state, payload).IsOk()
                || !asset->WriteData(u8"scene", payload.Bytes()).IsOk())
            {
                m_context->Notify(draconic::editor::NoticeKind::Error, u8"Apply to Prefab failed.");
                return;
            }
            Array<byte> bytes;
            for (byte b : payload.Bytes()) { bytes.PushBack(b); }
            const Guid prefabId = state->prefabId;
            EditorContext* context = m_context;
            if (m_scenes != nullptr)
            {
                m_scenes->ForEachScene([&](dscene::Scene& scene) {
                    const u32 rebuilt = dscene::RebuildPrefabInstances(
                        scene, prefabId, Span<const byte>{ bytes.Data(), bytes.Size() });
                    if (rebuilt > 0 && context->Resources() != nullptr)
                    {
                        dscene::ResolveSceneResources(scene, *context->Resources());
                    }
                });
            }
            // An open editor page on the prefab itself shows the TEMPLATE (plain entities,
            // not an instance) - the rebuild above can't reach it; tell it to refresh.
            for (const UniquePtr<draconic::editor::EditorPage>& open : m_context->OpenPages())
            {
                if (open->InstanceId() == prefabId) { open->OnAssetExternallyModified(); }
            }
            String message(u8"Applied to prefab '");
            message += asset->Name();
            message += u8"' (not undoable - the asset changed).";
            m_context->Notify(draconic::editor::NoticeKind::Success, message.AsView());
        }

        // Revert-instance entry point: destructive + not undoable, so it confirms first.
        // Non-member children under the instance are destroyed too - the dialog says so.
        void RevertInstance(const Guid& rootId)
        {
            if (m_scene == nullptr || m_context->Project() == nullptr
                || m_content->Context == nullptr)
            {
                return;
            }
            if (m_scene->FindPrefabInstanceByRoot(rootId) == nullptr) { return; }
            dscene::EntityHandle root = m_scene->FindEntity(rootId);
            String message(u8"Revert '");
            message += m_scene->GetEntityName(root);
            message += u8"' to its prefab? All overrides on this instance are discarded, and "
                       u8"any non-prefab entities parented under it are destroyed. This cannot "
                       u8"be undone.";
            SceneEditorPage* page = this;
            RefPtr<draconic::ui::Dialog> dialog =
                draconic::ui::Dialog::Confirm(u8"Revert Instance", message.AsView());
            dialog->OnClosed.Add(
                draconic::ui::Event<void(draconic::ui::Dialog*, draconic::ui::DialogResult)>::Handler{
                    [page, rootId](draconic::ui::Dialog*, draconic::ui::DialogResult result) {
                        if (result != draconic::ui::DialogResult::OK) { return; }
                        // Deferred: the revert destroys entities (and their hierarchy rows),
                        // never mid-event-dispatch.
                        draconic::ui::UIContext* ctx = page->m_content->Context;
                        if (ctx == nullptr) { return; }
                        ctx->MutationQueueRef().QueueAction(Function<void()>{
                            [page, rootId]() { page->RevertInstanceNow(rootId); } });
                    } });
            dialog->Show(m_content->Context);
        }

        // Revert-instance: discard this instance's deltas (respawn from the current template,
        // placement kept). Not undoable.
        void RevertInstanceNow(const Guid& rootId)
        {
            if (m_scene == nullptr || m_context->Project() == nullptr) { return; }
            dscene::Scene::PrefabInstanceState* state = m_scene->FindPrefabInstanceByRoot(rootId);
            if (state == nullptr) { return; }
            draconic::content::Instance* asset =
                m_context->Project()->SourceDb().GetInstance(state->prefabId);
            UniquePtr<IStream> payload = (asset != nullptr) ? asset->ReadData(u8"scene")
                                                            : UniquePtr<IStream>{};
            if (payload.Get() == nullptr)
            {
                m_context->Notify(draconic::editor::NoticeKind::Warning,
                                  u8"The instance's prefab asset no longer exists.");
                return;
            }
            Array<byte> bytes;
            bytes.Resize(static_cast<usize>(payload->Size()));
            (void)payload->Read(bytes.Data(), bytes.Size());
            if (dscene::RevertPrefabInstance(*m_scene, rootId,
                                             Span<const byte>{ bytes.Data(), bytes.Size() }))
            {
                if (m_context->Resources() != nullptr)
                {
                    dscene::ResolveSceneResources(*m_scene, *m_context->Resources());
                }
                m_context->Notify(draconic::editor::NoticeKind::Info,
                                  u8"Instance reverted to its prefab (not undoable).");
            }
        }

        // Spawn an instance under `parent` (nil = scene root) via the asset picker.
        void PickAndSpawnPrefab(const Guid& parent)
        {
            if (m_context->Project() == nullptr || m_content->Context == nullptr) { return; }
            Array<String> typeNames;
            typeNames.PushBack(String(u8"PrefabDocument"));
            auto dialog = MakeRef<draconic::editor::app::AssetPickerDialog>(
                DefaultAllocator(), *m_context, Move(typeNames));
            SceneEditorPage* page = this;
            dialog->OnPicked = [page, parent](const Guid& picked) {
                if (picked.IsNil() || page->m_context->Project() == nullptr) { return; }
                draconic::content::Instance* prefab =
                    page->m_context->Project()->SourceDb().GetInstance(picked);
                UniquePtr<IStream> payload =
                    (prefab != nullptr) ? prefab->ReadData(u8"scene") : UniquePtr<IStream>{};
                if (payload.Get() == nullptr)
                {
                    page->m_context->Notify(draconic::editor::NoticeKind::Warning,
                                            u8"Prefab has no content yet (save it once first).");
                    return;
                }
                Array<byte> bytes;
                bytes.Resize(static_cast<usize>(payload->Size()));
                (void)payload->Read(bytes.Data(), bytes.Size());
                (void)page->m_editContext->SpawnPrefabInstance(picked, Move(bytes), parent);
            };
            dialog->Show(m_content->Context);
        }

        // The asset changed under this page (apply-to-prefab from another page): wipe and
        // reload so the split-view prefab editor shows the new template. Unsaved edits are
        // never clobbered - the page just warns instead.
        void OnAssetExternallyModified() override
        {
            if (m_scene == nullptr || m_context->Project() == nullptr) { return; }
            draconic::content::Instance* instance =
                m_context->Project()->SourceDb().GetInstance(InstanceId());
            if (instance == nullptr) { return; }
            if (IsDirty())
            {
                String message(u8"'");
                message += m_title;
                message += u8"' changed on disk but has unsaved edits here - not refreshed.";
                m_context->Notify(draconic::editor::NoticeKind::Warning, message.AsView());
                return;
            }

            // Same-guid entities come back from the stream; undo history predates the reload
            // and would replay onto the old content, so it goes.
            while (m_scene->GetFirstRoot().IsAssigned())
            {
                m_scene->DestroyEntity(m_scene->GetFirstRoot());
            }
            m_scene->ClearPrefabInstances();
            m_editContext->EntitySelection().Clear();
            Commands().Clear();

            if (dscene::LoadScene(*instance, *m_scene).IsOk())
            {
                if (m_context->Resources() != nullptr)
                {
                    dscene::ResolveSceneResources(*m_scene, *m_context->Resources());
                }
                if (m_scene->PendingPrefabInstanceCount() > 0)
                {
                    EditorContext* context = m_context;
                    dscene::ResolveScenePrefabs(*m_scene,
                        Function<UniquePtr<IStream>(const Guid&)>{
                            [context](const Guid& prefabId) -> UniquePtr<IStream> {
                                draconic::content::Instance* prefab =
                                    context->Project()->SourceDb().GetInstance(prefabId);
                                return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                           : UniquePtr<IStream>{};
                            } });
                    if (m_context->Resources() != nullptr)
                    {
                        dscene::ResolveSceneResources(*m_scene, *m_context->Resources());
                    }
                }
            }
            ClearDirty();   // Commands().Clear() notifies OnChanged, which marks dirty
        }

        [[nodiscard]] Status Save() override
        {
            if (m_scene == nullptr || m_context->Project() == nullptr) { return Status{ ErrorCode::NotFound }; }
            draconic::content::Instance* instance = m_context->Project()->SourceDb().GetInstance(InstanceId());
            if (instance == nullptr) { return Status{ ErrorCode::NotFound }; }

            const bool isPrefab = instance->TypeName() == StringView(u8"PrefabDocument");
            if (isPrefab)
            {
                usize rootCount = 0;
                for (dscene::EntityHandle r = m_scene->GetFirstRoot(); r.IsAssigned();
                     r = m_scene->GetNextSibling(r))
                {
                    ++rootCount;
                }
                if (rootCount > 1)
                {
                    m_context->Notify(draconic::editor::NoticeKind::Warning,
                                      u8"A prefab needs exactly one root entity - parent "
                                      u8"everything under a single root, then save.");
                    return Status{ ErrorCode::InvalidArgument };
                }
            }
            const Status saved = isPrefab ? dscene::SavePrefab(*m_scene, *instance)
                                          : dscene::SaveScene(*m_scene, *instance);
            if (saved.IsOk())
            {
                ClearDirty();
                DRACONIC_LOG_INFO(u8"Editor", u8"saved {} '{}'",
                                  isPrefab ? StringView(u8"prefab") : StringView(u8"scene"), m_title);
                // Template changed: rebuild this prefab's instances in every OTHER open
                // scene, preserving their deltas (capture -> respawn -> reapply).
                if (isPrefab && m_scenes != nullptr)
                {
                    UniquePtr<IStream> payload = instance->ReadData(u8"scene");
                    if (payload.Get() != nullptr)
                    {
                        Array<byte> bytes;
                        bytes.Resize(static_cast<usize>(payload->Size()));
                        (void)payload->Read(bytes.Data(), bytes.Size());
                        const Guid prefabId = InstanceId();
                        dscene::Scene* self = m_scene;
                        EditorContext* context = m_context;
                        m_scenes->ForEachScene([&](dscene::Scene& other) {
                            if (&other == self) { return; }
                            const u32 rebuilt = dscene::RebuildPrefabInstances(
                                other, prefabId, Span<const byte>{ bytes.Data(), bytes.Size() });
                            if (rebuilt > 0 && context->Resources() != nullptr)
                            {
                                dscene::ResolveSceneResources(other, *context->Resources());
                            }
                        });
                    }
                }
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
            // Simulate mode: transforms belong to the running systems; gizmo drags would fight
            // them (and their commands are refused by the locked stack anyway).
            if (!m_gizmos || m_isSimulating) { return false; }
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

        // === Viewport toolbar (gizmo mode/space/grid) ===

        void BuildViewportToolbar()
        {
            namespace edapp = draconic::editor::app;
            m_toolbar = MakeRef<tk::Toolbar>(DefaultAllocator());
            edapp::EditorIcons& icons = edapp::EditorIcons::Get();
            GizmoController* gizmos = m_gizmos.Get();
            auto icon = [](draconic::ui::SVGDrawable* drawable) {
                return Function<void(draconic::ui::UIDrawContext&, Rectangle)>{
                    [drawable](draconic::ui::UIDrawContext& ctx, Rectangle rect) {
                        if (drawable != nullptr) { drawable->Draw(ctx, rect); }
                    } };
            };

            m_translateToggle = m_toolbar->AddToggle(u8"");
            m_translateToggle->SetIcon(icon(icons.translate.Get()));
            m_translateToggle->OnCheckedChanged.Add([gizmos](tk::ToolbarToggle*, bool value) {
                if (value) { gizmos->SetMode(GizmoMode::Translate); }
            });
            m_rotateToggle = m_toolbar->AddToggle(u8"");
            m_rotateToggle->SetIcon(icon(icons.rotate.Get()));
            m_rotateToggle->OnCheckedChanged.Add([gizmos](tk::ToolbarToggle*, bool value) {
                if (value) { gizmos->SetMode(GizmoMode::Rotate); }
            });
            m_scaleToggle = m_toolbar->AddToggle(u8"");
            m_scaleToggle->SetIcon(icon(icons.scale.Get()));
            m_scaleToggle->OnCheckedChanged.Add([gizmos](tk::ToolbarToggle*, bool value) {
                if (value) { gizmos->SetMode(GizmoMode::Scale); }
            });

            m_toolbar->AddSeparator();

            // One toggle whose icon + label read the LIVE space (checked = world).
            m_spaceToggle = m_toolbar->AddToggle(u8"World");
            m_spaceToggle->SetIcon(Function<void(draconic::ui::UIDrawContext&, Rectangle)>{
                [gizmos, &icons](draconic::ui::UIDrawContext& ctx, Rectangle rect) {
                    draconic::ui::SVGDrawable* drawable = (gizmos->Space() == GizmoSpace::World)
                        ? icons.worldSpace.Get() : icons.localSpace.Get();
                    if (drawable != nullptr) { drawable->Draw(ctx, rect); }
                } });
            m_spaceToggle->OnCheckedChanged.Add([gizmos](tk::ToolbarToggle* toggle, bool value) {
                gizmos->SetSpace(value ? GizmoSpace::World : GizmoSpace::Local);
                toggle->SetText(value ? StringView(u8"World") : StringView(u8"Local"));
            });

            m_toolbar->AddSeparator();

            ScenePage_GridToggleInit();

            // Spacer pushes the simulation cluster to the right edge (Sedulous toolbar shape).
            {
                auto spacer = MakeRef<draconic::ui::Panel>(DefaultAllocator());
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                m_toolbar->AddView(spacer.Get(), lp);
            }

            // === Simulate (snapshot -> run -> restore; phase-8a half of play-in-editor) ===
            SceneEditorPage* self = this;
            m_playButton = m_toolbar->AddButton(u8"Play");
            m_playButton->OnClick.Add([self](tk::ToolbarButton*) { self->StartSimulation(); });
            m_pauseToggle = m_toolbar->AddToggle(u8"Pause");
            m_pauseToggle->OnCheckedChanged.Add([self](tk::ToolbarToggle*, bool value) {
                self->PauseSimulation(value);
            });
            m_stopButton = m_toolbar->AddButton(u8"Stop");
            m_stopButton->OnClick.Add([self](tk::ToolbarButton*) { self->StopSimulation(); });
            // The at-a-glance state readout (user report: Play gave no visual indication).
            m_simLabel = MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView(u8""));
            m_simLabel->FontSize.SetValue(13.0f);
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Height = draconic::ui::SizeSpec::Match();
                m_toolbar->AddView(m_simLabel.Get(), lp);
            }
            RefreshSimToolbar();
        }

        // === Simulation lifecycle (Sedulous SceneEditorPage port) ===

        /// Snapshot the scene and flip it live: Scene::Start() fires OnSceneStarted on every
        /// system, SimulationEnabled un-freezes simulation-only work, and the command stack
        /// LOCKS (runtime mutations don't belong on the edit history; undoing into entities
        /// the restore recreates is a guid minefield). No-op if already simulating.
        void StartSimulation()
        {
            if (m_isSimulating || m_scene == nullptr) { return; }
            m_simSnapshot = dscene::SceneSnapshot::Capture(*m_scene);
            if (!m_simSnapshot)
            {
                DRACONIC_LOG_ERROR(u8"Editor", u8"Simulate: scene snapshot capture failed");
                return;
            }
            m_scene->Start();
            m_scene->SetSimulationEnabled(true);
            Commands().SetLocked(true);
            m_isSimulating = true;
            m_isPaused = false;
            RefreshSimToolbar();
        }

        /// Freeze/resume the running simulation (SimulationEnabled only - the Start/Stop
        /// system callbacks are for the big transitions, not the per-frame pause).
        void PauseSimulation(bool paused)
        {
            if (!m_isSimulating || m_scene == nullptr) { return; }
            m_isPaused = paused;
            m_scene->SetSimulationEnabled(!paused);
            RefreshSimToolbar();
        }

        /// Scene::Stop(), then restore the snapshot INTO THE SAME Scene instance (borrowed
        /// scene pointers stay valid; the guid-keyed selection re-resolves against restored
        /// entities - runtime-spawned ones drop out naturally). No-op if not simulating.
        void StopSimulation()
        {
            if (!m_isSimulating || m_scene == nullptr) { return; }
            m_scene->Stop();
            if (m_simSnapshot)
            {
                draconic::resource::ResourceManager* resources =
                    (m_context != nullptr) ? m_context->Resources() : nullptr;
                if (!m_simSnapshot->Restore(*m_scene, resources).IsOk())
                {
                    DRACONIC_LOG_ERROR(u8"Editor", u8"Simulate: snapshot restore failed");
                }
                m_simSnapshot = nullptr;
            }
            m_scene->SetSimulationEnabled(false);
            Commands().SetLocked(false);
            m_isSimulating = false;
            m_isPaused = false;
            // Hierarchy/inspector re-sync off the scene's revisions next frame (the restore
            // drained + recreated every entity, which bumps them).
            RefreshSimToolbar();
        }

        void RefreshSimToolbar()
        {
            if (m_playButton == nullptr) { return; }
            m_playButton->IsEnabled = !m_isSimulating;
            m_pauseToggle->IsEnabled = m_isSimulating;
            m_stopButton->IsEnabled = m_isSimulating;
            m_pauseToggle->SetIsChecked(m_isPaused);
            if (m_simLabel.Get() != nullptr)
            {
                if (!m_isSimulating) { m_simLabel->SetText(u8""); }
                else
                {
                    m_simLabel->SetText(m_isPaused ? StringView(u8" PAUSED ")
                                                   : StringView(u8" SIMULATING "));
                    m_simLabel->TextColor.SetValue(Optional<Color>(m_isPaused
                        ? Color{ 0.95f, 0.85f, 0.4f, 1.0f }     // amber
                        : Color{ 0.95f, 0.55f, 0.35f, 1.0f })); // orange
                }
            }
            m_playButton->Invalidate();
            m_pauseToggle->Invalidate();
            m_stopButton->Invalidate();
        }

        // (split out so the lambda below can live next to its state)
        void ScenePage_GridToggleInit()
        {
            namespace edapp = draconic::editor::app;
            m_gridToggle = m_toolbar->AddToggle(u8"");
            m_gridToggle->SetIcon(Function<void(draconic::ui::UIDrawContext&, Rectangle)>{
                [](draconic::ui::UIDrawContext& ctx, Rectangle rect) {
                    if (auto* drawable = edapp::EditorIcons::Get().grid.Get()) { drawable->Draw(ctx, rect); }
                } });
            SceneEditorPage* self = this;
            m_gridToggle->OnCheckedChanged.Add([self](tk::ToolbarToggle*, bool value) {
                self->m_showGrid = value;
            });
        }

        // Reflect externally-driven state (the W/E/R keys, X space toggle) back into the
        // toolbar. SetIsChecked no-ops when unchanged, and the mode handlers only act on
        // true, so this settles without feedback loops.
        void SyncToolbar()
        {
            if (m_toolbar.Get() == nullptr || !m_gizmos) { return; }
            const GizmoMode mode = m_gizmos->Mode();
            m_translateToggle->SetIsChecked(mode == GizmoMode::Translate);
            m_rotateToggle->SetIsChecked(mode == GizmoMode::Rotate);
            m_scaleToggle->SetIsChecked(mode == GizmoMode::Scale);
            const bool world = (m_gizmos->Space() == GizmoSpace::World);
            m_spaceToggle->SetIsChecked(world);
            m_gridToggle->SetIsChecked(m_showGrid);
        }

        // Position markers for every entity (small cross; selected = brighter + boxed) - empty
        // entities have no renderable, so the editor gives them a visual anchor. The selection
        // box hugs the entity's REAL renderable bounds when it has any (mesh AABB in the
        // entity's oriented frame; instanced sets use their merged world bounds); the small
        // fixed cube remains the meshless fallback.
        void DrawEntityMarkers(drender::debug::DebugDraw& dd)
        {
            if (!m_editContext) { return; }
            Selection<Guid>& selection = m_editContext->EntitySelection();
            dscene::Scene& scene = *m_scene;
            auto* meshes = scene.GetSystem<drender::MeshComponentManager>();
            auto* instanced = scene.GetSystem<drender::InstancedMeshComponentManager>();
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
                if (!selected) { return; }

                if (meshes != nullptr)
                {
                    if (drender::MeshComponent* mc = meshes->Get(e))
                    {
                        if (draconic::geometry::StaticMesh* mesh = mc->mesh.Get())
                        {
                            dd.DrawTransformedBox(mesh->bounds.min, mesh->bounds.max, world, color);
                            return;
                        }
                    }
                }
                if (instanced != nullptr)
                {
                    if (drender::InstancedMeshComponent* imc = instanced->Get(e))
                    {
                        if (imc->mesh.Get() != nullptr && imc->Count() > 0 && imc->cachedRadius > 0.0f)
                        {
                            // Merged world bounds (kept current by extraction's compose pass).
                            const f32 r = imc->cachedRadius;
                            dd.DrawWireBoxCenter(imc->cachedCenter, Float3{ r, r, r }, color);
                            return;
                        }
                    }
                }
                dd.DrawWireBoxCenter(p, Float3{ 0.35f, 0.35f, 0.35f }, color);
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
        RefPtr<tk::Toolbar> m_toolbar;
        tk::ToolbarButton* m_playButton = nullptr;   // borrowed (toolbar-owned)
        tk::ToolbarToggle* m_pauseToggle = nullptr;
        tk::ToolbarButton* m_stopButton = nullptr;
        RefPtr<draconic::ui::Label> m_simLabel;
        UniquePtr<dscene::SceneSnapshot> m_simSnapshot;
        bool m_isSimulating = false;
        bool m_isPaused = false;
        tk::ToolbarToggle* m_translateToggle = nullptr;   // borrowed (toolbar-owned)
        tk::ToolbarToggle* m_rotateToggle = nullptr;
        tk::ToolbarToggle* m_scaleToggle = nullptr;
        tk::ToolbarToggle* m_spaceToggle = nullptr;
        tk::ToolbarToggle* m_gridToggle = nullptr;
        bool m_showGrid = true;
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

    // Prefab assets open on the SAME editor page - a prefab payload IS a scene stream (the
    // page's Save branches to SavePrefab + rebuilds open instances).
    class PrefabEditorPageFactory final : public IEditorPageFactory
    {
    public:
        PrefabEditorPageFactory(rt::IApplicationHost& host, uirt::UIHost& uiHost)
            : m_host(&host), m_uiHost(&uiHost) {}

        [[nodiscard]] const TypeInfo* PrimaryType() const override
        {
            return &dscene::PrefabDocument::StaticType();
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

    // Create a fresh empty prefab instance under "Prefabs/", named uniquely (Prefab,
    // Prefab2, ...). Content arrives when the user saves the opened page (an unsaved prefab
    // has no payload; spawning one warns).
    inline draconic::content::Instance* CreatePrefabInstance(EditorContext& context,
                                                             draconic::content::Group* target = nullptr)
    {
        EditorProject* project = context.Project();
        if (project == nullptr) { return nullptr; }

        draconic::content::Group* prefabs = target;
        if (prefabs == nullptr)
        {
            draconic::content::Group* root = project->SourceDb().RootGroup();
            prefabs = root->GetGroup(u8"Prefabs");
            if (prefabs == nullptr) { prefabs = root->CreateGroup(u8"Prefabs"); }
        }
        if (prefabs == nullptr) { return nullptr; }

        String name(u8"Prefab");
        for (i32 counter = 2; prefabs->GetInstance(name.AsView()) != nullptr; ++counter)
        {
            name = String(u8"Prefab");
            name.PushBack(static_cast<utf8char>('0' + (counter % 10)));
            if (counter >= 10) { name.PushBack(static_cast<utf8char>('0' + (counter / 10 % 10))); }
        }

        draconic::content::Instance* instance =
            prefabs->CreateInstance(name.AsView(), dscene::PrefabDocument::StaticType());
        if (instance == nullptr) { return nullptr; }
        dscene::PrefabDocument doc;
        doc.name = name;
        if (!instance->WriteObject(doc).IsOk()) { return nullptr; }

        // Seed one root entity so the prefab opens in the enforced single-root shape and is
        // spawnable immediately (an empty payload can't spawn).
        dscene::Scene seed(u8"seed");
        dscene::EntityHandle root = seed.CreateEntity(name.AsView());
        MemoryStream buffer;
        if (dscene::CapturePrefab(seed, root, buffer).IsOk())
        {
            (void)instance->WriteData(u8"scene", buffer.Bytes());
        }
        return instance;
    }

    // Create a fresh scene instance in the project's source DB under "Scenes/", named uniquely
    // (Scene, Scene2, ...). Writes the SceneDocument primary so the instance materializes; the
    // page treats the missing "scene" stream as an empty scene.
    inline draconic::content::Instance* CreateSceneInstance(EditorContext& context,
                                                             draconic::content::Group* target = nullptr)
    {
        EditorProject* project = context.Project();
        if (project == nullptr) { return nullptr; }

        draconic::content::Group* scenes = target;
        if (scenes == nullptr)
        {
            draconic::content::Group* root = project->SourceDb().RootGroup();
            scenes = root->GetGroup(u8"Scenes");
            if (scenes == nullptr) { scenes = root->CreateGroup(u8"Scenes"); }
        }
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

        // Seed default content: a directional Sun so a fresh scene is LIT out of the box
        // (with no light, meshes render in the dim flat ambient fallback and read as broken -
        // the classic "why is my duck untextured"). An authored entity, not editor magic: it
        // saves with the scene, shows in the hierarchy, and is free to edit or delete.
        {
            dscene::Scene seeded(name.AsView());
            seeded.AddSystem<draconic::render::LightComponentManager>();
            const dscene::EntityHandle sun = seeded.CreateEntity(u8"Sun");
            Transform t;
            // Shines along the entity's forward (-Z): tilt ~60 deg down, a slight compass yaw
            // (the Sandbox key-light default) so shading has direction.
            t.rotation = Quaternion::FromAxisAngle(Float3{ 0, 1, 0 }, 0.35f)
                       * Quaternion::FromAxisAngle(Float3{ 1, 0, 0 }, -1.05f);
            seeded.SetLocalTransform(sun, t);
            draconic::render::LightComponent& light =
                seeded.GetSystem<draconic::render::LightComponentManager>()->Add(sun);
            light.castsShadows = true;   // intensity stays the component default (the value the
                                         // duck-scene fix was verified with)
            (void)dscene::SaveScene(seeded, *instance);
        }
        return instance;
    }

    inline void RegisterSceneEditor(EditorContext& context, rt::IApplicationHost& host, uirt::UIHost& uiHost)
    {
        GlobalTypeRegistry().Register(dscene::SceneDocument::StaticType());
        RegisterSerializable<dscene::SceneDocument>();
        GlobalTypeRegistry().Register(dscene::PrefabDocument::StaticType());
        RegisterSerializable<dscene::PrefabDocument>();

        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<SceneEditorPageFactory>(host, uiHost), DefaultAllocator()));
        context.Pages().Register(UniquePtr<IEditorPageFactory>(
            DefaultAllocator().New<PrefabEditorPageFactory>(host, uiHost), DefaultAllocator()));

        EditorContext::AssetCreator creator;
        creator.label = String(u8"Scene");
        creator.create = [](EditorContext& ctx, draconic::content::Group* group) {
            return CreateSceneInstance(ctx, group);
        };
        creator.setsDefaultScene = true;
        context.RegisterCreator(Move(creator));

        EditorContext::AssetCreator prefabCreator;
        prefabCreator.label = String(u8"Prefab");
        prefabCreator.create = [](EditorContext& ctx, draconic::content::Group* group) {
            return CreatePrefabInstance(ctx, group);
        };
        context.RegisterCreator(Move(prefabCreator));
    }
}
