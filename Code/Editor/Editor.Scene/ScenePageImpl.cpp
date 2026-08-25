// Editor::Scene - :page partition.
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

module editor.scene;

import foundation.core;
import foundation.content;
import foundation.rhi;
import foundation.graphics;
import foundation.shell;
import foundation.runtime;
import foundation.runtime.client;
import engine.defaultapp;
import engine.gameinstance; // GameInstance (the Game tab's run; multi-instance factory)
import foundation.scene;
import engine.scene;
import foundation.scene.resource;
import foundation.resource; // AsyncBindScope
import foundation.render;
import engine.render;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import engine.ui; // game-UI RenderTexture canvases (live in editing viewports)
import foundation.ui.viewport;
import foundation.vg.renderer;
import editor.core;
import editor.app;
import editor.camera;
import :camera_preview;
import :edit;
import :model_prefab;
import :game_page;
import :gizmo;
import :component_gizmos;
import :hierarchy;
import :inspector;

using namespace foundation::core;
namespace render = foundation::render;
namespace rhi = foundation::rhi;
namespace runtime = foundation::runtime;
namespace scene = foundation::scene;
namespace ui = foundation::ui;
namespace vg = foundation::vg;

namespace editor
{
    const TypeInfo* PrefabEditorPageFactory::PrimaryType() const
    {
        return &scene::PrefabDocument::StaticType();
    }

    UniquePtr<EditorPage> PrefabEditorPageFactory::CreatePage(EditorContext& context,
                                                              foundation::content::Instance& instance)
    {
        return UniquePtr<EditorPage>(
            DefaultAllocator().New<SceneEditorPage>(context, *m_host, *m_uiHost, instance),
            DefaultAllocator());
    }
    void SceneEditorPage::OnUpdate(runtime::IApplicationHost&, f32 dt)
    {
        EnsureViewportBound();
        if (m_hostWindow == nullptr)
        {
            return;
        }

        if (m_hierarchy)
        {
            m_hierarchy->Refresh();
        } // scene revision -> tree rebuild
        if (m_inspector)
        {
            m_inspector->Refresh();
        } // selection/structure -> grid rebuild

        m_viewport->SyncInputRegion();
        // Same keyboard arbitration as the Game page: editor-UI focus elsewhere (a dialog
        // field, the inspector) drops surface focus, so camera keys and Simulate input
        // never fire while the user is typing into an editor widget.
        m_router->SetExternalCapture(false, m_viewport->HostKeyboardFocusElsewhere());
        m_router->Update();
        const bool viewportActive = m_viewport->IsHovered() || m_viewport->IsFocused();
        if (viewportActive)
        {
            // First-consumer rule: a modal viewport tool (terrain sculpt) owns the wheel to resize
            // its brush, so the camera must not ALSO dolly on scroll while one is active.
            const bool modalToolActive =
                m_viewportTools.ActiveTool() != nullptr && m_viewportTools.ActiveTool() != m_selectTool;
            m_camera.Update(m_viewport->Keyboard(), m_viewport->Mouse(), dt, !modalToolActive);
        }
        else
        {
            // The I2 stuck-mouse fix: Tab-capture's ONLY off-switch lived inside the active
            // branch, and relative mode makes losing activity easy - release the OS grab the
            // moment the viewport stops being the input owner.
            m_camera.ReleaseCapture(m_viewport->Mouse());
        }
        (void)UpdateViewportTools(viewportActive, dt); // picking lives inside the select tool now
        // Mount/clear the active tool's settings panel in the bottom dock (safe here - a frame
        // boundary, never mid-event; the mount tears down the previous panel view).
        if (m_toolPanelHost)
        {
            m_toolPanelHost->Sync();
        }
        if (m_propAnimPanel)
        {
            // Advances editor playback + gates preview to EDIT (no preview/playback under Simulate).
            m_propAnimPanel->Tick(dt, m_isSimulating);
        }
        // The page is this scene's run scope: drain its bus once per frame (events emitted
        // during last frame's Simulate tick arrive now). Cheap no-op when empty/not simulating.
        m_pageEvents.Drain();

        // Per-scene debug draw (shows only where THIS scene renders; lists clear in
        // EndRendering, so re-accumulate every frame): ground grid + origin axes + entity
        // markers (selected = boxed and brighter) + gizmos.
        if (m_render != nullptr && m_scene != nullptr)
        {
            // Draw the editor overlay (grid + markers + gizmos) into THIS viewport's own keyed
            // debug list, not the per-scene one - so it renders only in the main viewport, never in
            // the camera-preview inset (a second view of the same scene). RenderScene below passes
            // the matching viewportKey; the preview passes its own (empty) key. Task #118.
            render::debug::DebugDraw& dd = m_render->DebugView(m_viewport.Get());
            if (m_showGrid)
            {
                dd.DrawGrid(Float3{0, 0, 0}, 20.0f, 20, Color{0.35f, 0.35f, 0.38f, 1.0f});
                dd.DrawLine(Float3{0, 0, 0}, Float3{1, 0, 0}, Color{0.9f, 0.2f, 0.2f, 1.0f});
                dd.DrawLine(Float3{0, 0, 0}, Float3{0, 1, 0}, Color{0.2f, 0.9f, 0.2f, 1.0f});
                dd.DrawLine(Float3{0, 0, 0}, Float3{0, 0, 1}, Color{0.2f, 0.4f, 0.95f, 1.0f});
            }
            DrawEntityMarkers(dd);
            DrawGizmos(dd);
            if (m_propAnimPanel)
            {
                m_propAnimPanel->DrawOverlay(dd); // the live-preview entity marker
            }
        }
        UpdateCameraPreview(); // task #118: selection/pin -> preview visibility + target
        SyncToolbar();
    }

    void SceneEditorPage::OnRenderWindow(runtime::IApplicationHost&,
                                         foundation::graphics::FrameContext& frame)
    {
        if (!m_viewport->IsReady() || !frame.valid)
        {
            return;
        }
        if (m_render == nullptr || !m_render->IsReady() || m_scene == nullptr)
        {
            return;
        }
        const u32 w = m_viewport->RenderWidth();
        const u32 h = m_viewport->RenderHeight();
        if (w == 0 || h == 0)
        {
            return;
        }

        // Skip hidden viewports (inactive dock tabs set Visibility=Gone up the ancestor
        // chain) - no GPU work for content nobody can see (Sedulous does the same).
        if (!m_viewport->IsEffectivelyVisible())
        {
            return;
        }

        if (!m_renderedOnce)
        {
            m_renderedOnce = true;
            LOG_DEBUG(u8"Editor", u8"scene page '{}' first frame ({}x{})", m_title, w, h);
        }

        // RenderTexture canvases stay LIVE in editing viewports too (WYSIWYG - an
        // in-world screen must not show stale/black content while its scene is being
        // edited; Simulate renders through this same path). Same host seam as the
        // player/Game tab; the subsystem draws at most once per UI frame, so several
        // open pages (or a running Game tab) share one draw of every RT canvas.
        if (m_gameUI != nullptr)
        {
            m_gameUI->RenderCanvasTextures(*frame.encoder, static_cast<i32>(frame.frameIndex));
        }

        render::ViewCamera camera;
        camera.view = Float4x4::LookAtRH(m_camera.position, m_camera.position + m_camera.Forward(),
                                         m_camera.Up());
        camera.projection = Float4x4::PerspectiveFovRH(
            1.0472f, static_cast<f32>(w) / static_cast<f32>(h), 0.1f, 1000.0f);
        camera.position = m_camera.position;
        camera.farZ = 1000.0f;

        render::CameraOverride cameraOverride;
        cameraOverride.camera = camera;
        cameraOverride.clearColor = Color{m_viewport->ClearColor.r, m_viewport->ClearColor.g,
                                          m_viewport->ClearColor.b, m_viewport->ClearColor.a};

        // The graph imports the color target and owns its transitions: from the viewport's
        // tracked state (Undefined right after create/resize) to ShaderRead for the UI's
        // sampling - the Sandbox offscreen pattern.
        render::TargetState targetState;
        targetState.texture = m_viewport->ColorTexture();
        targetState.currentState = m_viewport->ColorState();
        targetState.finalState = rhi::ResourceState::ShaderRead;

        m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(), w,
                              h, render::ViewportRect{0, 0, w, h}, &cameraOverride, targetState,
                              &m_postOverride, /*viewportKey*/ m_viewport.Get());
        m_viewport->SetColorState(rhi::ResourceState::ShaderRead);

        RenderCameraPreview(); // task #118: a second RenderScene through the previewed camera
    }

    void SceneEditorPage::CreatePrefabFromEntity(const Guid& entityId)
    {
        if (m_scene == nullptr || m_context->Project() == nullptr)
        {
            return;
        }
        const scene::EntityHandle live = m_editContext->Resolve(entityId);
        if (!live.IsAssigned())
        {
            return;
        }

        MemoryStream payload;
        if (!scene::CapturePrefab(*m_scene, live, payload).IsOk())
        {
            m_context->Notify(editor::NoticeKind::Error, u8"Prefab capture failed.");
            return;
        }

        foundation::content::Group* root = m_context->Project()->SourceDb().RootGroup();
        foundation::content::Group* prefabs = root->GetGroup(u8"Prefabs");
        if (prefabs == nullptr)
        {
            prefabs = root->CreateGroup(u8"Prefabs");
        }
        String base(m_scene->GetEntityName(live));
        if (base.IsEmpty())
        {
            base = String(u8"Prefab");
        }
        const String name = prefabs->UniqueInstanceName(base.AsView());
        foundation::content::Instance* asset =
            prefabs->CreateInstance(name.AsView(), scene::PrefabDocument::StaticType());
        if (asset == nullptr)
        {
            return;
        }
        scene::PrefabDocument doc;
        doc.name = name;
        if (!asset->WriteObject(doc).IsOk() || !asset->WriteData(u8"scene", payload.Bytes()).IsOk())
        {
            m_context->Notify(editor::NoticeKind::Error, u8"Prefab asset write failed.");
            return;
        }

        Array<byte> bytes;
        const Span<const byte> view = payload.Bytes();
        bytes.Reserve(view.Size());
        for (byte b : view)
        {
            bytes.PushBack(b);
        }
        const Guid instanceRoot =
            m_editContext->ReplaceWithPrefabInstance(entityId, asset->Id(), Move(bytes));
        if (!instanceRoot.IsNil())
        {
            String message(u8"Created prefab '");
            message += name;
            message += u8"'.";
            m_context->Notify(editor::NoticeKind::Success, message.AsView());
        }
    }

    void SceneEditorPage::ApplyInstanceToPrefab(const Guid& rootId)
    {
        if (m_scene == nullptr || m_context->Project() == nullptr || m_content->Context == nullptr)
        {
            return;
        }
        scene::Scene::PrefabInstanceState* state = m_scene->FindPrefabInstanceByRoot(rootId);
        if (state == nullptr)
        {
            return;
        }
        foundation::content::Instance* asset =
            m_context->Project()->SourceDb().GetInstance(state->prefabId);
        if (asset == nullptr)
        {
            m_context->Notify(editor::NoticeKind::Warning,
                              u8"The instance's prefab asset no longer exists.");
            return;
        }
        scene::EntityHandle root = m_scene->FindEntity(rootId);
        String message(u8"Apply '");
        message += m_scene->GetEntityName(root);
        message += u8"' to prefab '";
        message += asset->Name();
        message += u8"'? The prefab asset is rewritten and every instance in open scenes "
                   u8"updates to match. This cannot be undone.";
        SceneEditorPage* page = this;
        RefPtr<foundation::ui::Dialog> dialog =
            foundation::ui::Dialog::Confirm(u8"Apply to Prefab", message.AsView());
        dialog->OnClosed.Add(
            foundation::ui::Event<void(foundation::ui::Dialog*, foundation::ui::DialogResult)>::Handler{
                [page, rootId](foundation::ui::Dialog*, foundation::ui::DialogResult result)
                {
                    if (result != foundation::ui::DialogResult::OK)
                    {
                        return;
                    }
                    // Deferred: rebuilding instances destroys + respawns entities (and
                    // their hierarchy rows), never mid-event-dispatch.
                    foundation::ui::UIContext* ctx = page->m_content->Context;
                    if (ctx == nullptr)
                    {
                        return;
                    }
                    ctx->MutationQueueRef().QueueAction(Function<void()>{
                        [page, rootId]() { page->ApplyInstanceToPrefabNow(rootId); }});
                }});
        dialog->Show(m_content->Context);
    }

    void SceneEditorPage::ApplyInstanceToPrefabNow(const Guid& rootId)
    {
        if (m_scene == nullptr || m_context->Project() == nullptr)
        {
            return;
        }
        scene::Scene::PrefabInstanceState* state = m_scene->FindPrefabInstanceByRoot(rootId);
        if (state == nullptr)
        {
            return;
        }
        foundation::content::Instance* asset =
            m_context->Project()->SourceDb().GetInstance(state->prefabId);
        if (asset == nullptr)
        {
            m_context->Notify(editor::NoticeKind::Warning,
                              u8"The instance's prefab asset no longer exists.");
            return;
        }
        MemoryStream payload;
        if (!scene::CaptureInstanceAsTemplate(*m_scene, *state, payload,
                                              m_editContext->PrefabResolver())
                 .IsOk() ||
            !asset->WriteData(u8"scene", payload.Bytes()).IsOk())
        {
            m_context->Notify(editor::NoticeKind::Error, u8"Apply to Prefab failed.");
            return;
        }
        Array<byte> bytes;
        for (byte b : payload.Bytes())
        {
            bytes.PushBack(b);
        }
        const Guid prefabId = state->prefabId;
        EditorContext* context = m_context;
        if (m_scenes != nullptr)
        {
            m_scenes->ForEachScene(
                [&](scene::Scene& scene)
                {
                    const u32 rebuilt = scene::RebuildPrefabInstances(
                        scene, prefabId, Span<const byte>{bytes.Data(), bytes.Size()},
                        m_editContext->PrefabResolver());
                    if (rebuilt > 0 && context->Resources() != nullptr)
                    {
                        foundation::resource::AsyncBindScope asyncScope(*context->Resources());
                        scene::ResolveSceneResources(scene, *context->Resources());
                    }
                });
        }
        // An open editor page on the prefab itself shows the TEMPLATE (plain entities,
        // not an instance) - the rebuild above can't reach it; tell it to refresh.
        for (const UniquePtr<editor::EditorPage>& open : m_context->OpenPages())
        {
            if (open->InstanceId() == prefabId)
            {
                open->OnAssetExternallyModified();
            }
        }
        String message(u8"Applied to prefab '");
        message += asset->Name();
        message += u8"' (not undoable - the asset changed).";
        m_context->Notify(editor::NoticeKind::Success, message.AsView());
    }

    void SceneEditorPage::RevertInstance(const Guid& rootId)
    {
        if (m_scene == nullptr || m_context->Project() == nullptr || m_content->Context == nullptr)
        {
            return;
        }
        if (m_scene->FindPrefabInstanceByRoot(rootId) == nullptr)
        {
            return;
        }
        scene::EntityHandle root = m_scene->FindEntity(rootId);
        String message(u8"Revert '");
        message += m_scene->GetEntityName(root);
        message += u8"' to its prefab? All overrides on this instance are discarded, and "
                   u8"any non-prefab entities parented under it are destroyed. This cannot "
                   u8"be undone.";
        SceneEditorPage* page = this;
        RefPtr<foundation::ui::Dialog> dialog =
            foundation::ui::Dialog::Confirm(u8"Revert Instance", message.AsView());
        dialog->OnClosed.Add(
            foundation::ui::Event<void(foundation::ui::Dialog*, foundation::ui::DialogResult)>::Handler{
                [page, rootId](foundation::ui::Dialog*, foundation::ui::DialogResult result)
                {
                    if (result != foundation::ui::DialogResult::OK)
                    {
                        return;
                    }
                    // Deferred: the revert destroys entities (and their hierarchy rows),
                    // never mid-event-dispatch.
                    foundation::ui::UIContext* ctx = page->m_content->Context;
                    if (ctx == nullptr)
                    {
                        return;
                    }
                    ctx->MutationQueueRef().QueueAction(
                        Function<void()>{[page, rootId]() { page->RevertInstanceNow(rootId); }});
                }});
        dialog->Show(m_content->Context);
    }

    void SceneEditorPage::RevertInstanceNow(const Guid& rootId)
    {
        if (m_scene == nullptr || m_context->Project() == nullptr)
        {
            return;
        }
        scene::Scene::PrefabInstanceState* state = m_scene->FindPrefabInstanceByRoot(rootId);
        if (state == nullptr)
        {
            return;
        }
        foundation::content::Instance* asset =
            m_context->Project()->SourceDb().GetInstance(state->prefabId);
        UniquePtr<IStream> payload =
            (asset != nullptr) ? asset->ReadData(u8"scene") : UniquePtr<IStream>{};
        if (payload.Get() == nullptr)
        {
            m_context->Notify(editor::NoticeKind::Warning,
                              u8"The instance's prefab asset no longer exists.");
            return;
        }
        Array<byte> bytes;
        bytes.Resize(static_cast<usize>(payload->Size()));
        (void)payload->Read(bytes.Data(), bytes.Size());
        if (scene::RevertPrefabInstance(*m_scene, rootId,
                                        Span<const byte>{bytes.Data(), bytes.Size()},
                                        m_editContext->PrefabResolver()))
        {
            if (m_context->Resources() != nullptr)
            {
                foundation::resource::AsyncBindScope asyncScope(*m_context->Resources());
                scene::ResolveSceneResources(*m_scene, *m_context->Resources());
            }
            m_context->Notify(editor::NoticeKind::Info,
                              u8"Instance reverted to its prefab (not undoable).");
        }
    }

    void SceneEditorPage::PickAndSpawnPrefab(const Guid& parent)
    {
        if (m_context->Project() == nullptr || m_content->Context == nullptr)
        {
            return;
        }
        Array<String> typeNames;
        typeNames.PushBack(String(u8"PrefabDocument"));
        auto dialog = MakeRef<editor::app::AssetPickerDialog>(
            DefaultAllocator(), *m_context, Move(typeNames));
        SceneEditorPage* page = this;
        dialog->OnPicked = [page, parent](const Guid& picked)
        {
            if (picked.IsNil() || page->m_context->Project() == nullptr)
            {
                return;
            }
            foundation::content::Instance* prefab =
                page->m_context->Project()->SourceDb().GetInstance(picked);
            UniquePtr<IStream> payload =
                (prefab != nullptr) ? prefab->ReadData(u8"scene") : UniquePtr<IStream>{};
            if (payload.Get() == nullptr)
            {
                page->m_context->Notify(editor::NoticeKind::Warning,
                                        u8"Prefab has no content yet (save it once first).");
                return;
            }
            Array<byte> bytes;
            bytes.Resize(static_cast<usize>(payload->Size()));
            (void)payload->Read(bytes.Data(), bytes.Size());
            if (picked == page->InstanceId())
            {
                page->m_context->Notify(editor::NoticeKind::Warning,
                                        u8"A prefab cannot contain an instance of itself.");
                return;
            }
            (void)page->m_editContext->SpawnPrefabInstance(picked, Move(bytes), parent);
        };
        dialog->Show(m_content->Context);
    }

    void SceneEditorPage::OnAssetExternallyModified()
    {
        if (m_scene == nullptr || m_context->Project() == nullptr)
        {
            return;
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return;
        }
        if (IsDirty())
        {
            String message(u8"'");
            message += m_title;
            message += u8"' changed on disk but has unsaved edits here - not refreshed.";
            m_context->Notify(editor::NoticeKind::Warning, message.AsView());
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

        if (scene::LoadScene(*instance, *m_scene).IsOk())
        {
            if (m_context->Resources() != nullptr)
            {
                scene::ResolveSceneResources(*m_scene, *m_context->Resources());
            }
            if (m_scene->PendingPrefabInstanceCount() > 0)
            {
                EditorContext* context = m_context;
                scene::ResolveScenePrefabs(
                    *m_scene,
                    Function<UniquePtr<IStream>(const Guid&)>{
                        [context](const Guid& prefabId) -> UniquePtr<IStream>
                        {
                            foundation::content::Instance* prefab =
                                context->Project()->SourceDb().GetInstance(prefabId);
                            return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                       : UniquePtr<IStream>{};
                        }});
                if (m_context->Resources() != nullptr)
                {
                    scene::ResolveSceneResources(*m_scene, *m_context->Resources());
                }
            }
        }
        ClearDirty(); // Commands().Clear() notifies OnChanged, which marks dirty
    }

    void SceneEditorPage::OnSavedAs(foundation::content::Instance& instance)
    {
        editor::EditorPage::OnSavedAs(instance);
        m_title = String(instance.Name());
        // SavePrefab stamps the document name from the scene, so the fork gets its own.
        if (m_scene != nullptr)
        {
            m_scene->SetName(instance.Name());
        }
    }

    Status SceneEditorPage::Save()
    {
        if (m_scene == nullptr || m_context->Project() == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }
        foundation::content::Instance* instance =
            m_context->Project()->SourceDb().GetInstance(InstanceId());
        if (instance == nullptr)
        {
            return Status{ErrorCode::NotFound};
        }

        const bool isPrefab = instance->TypeName() == StringView(u8"PrefabDocument");
        if (isPrefab)
        {
            usize rootCount = 0;
            for (scene::EntityHandle r = m_scene->GetFirstRoot(); r.IsAssigned();
                 r = m_scene->GetNextSibling(r))
            {
                ++rootCount;
            }
            if (rootCount > 1)
            {
                m_context->Notify(editor::NoticeKind::Warning,
                                  u8"A prefab needs exactly one root entity - parent "
                                  u8"everything under a single root, then save.");
                return Status{ErrorCode::InvalidArgument};
            }
            bool selfReference = false;
            m_scene->ForEachPrefabInstance(
                [&](scene::Scene::PrefabInstanceState& state)
                {
                    if (state.prefabId == InstanceId())
                    {
                        selfReference = true;
                    }
                });
            if (selfReference)
            {
                m_context->Notify(editor::NoticeKind::Warning,
                                  u8"A prefab cannot contain an instance of itself - "
                                  u8"remove it, then save.");
                return Status{ErrorCode::InvalidArgument};
            }
        }
        const Status saved = isPrefab ? scene::SavePrefab(*m_scene, *instance)
                                      : scene::SaveScene(*m_scene, *instance);
        if (saved.IsOk())
        {
            ClearDirty();
            LOG_INFO(u8"Editor", u8"saved {} '{}'",
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
                    scene::Scene* self = m_scene;
                    EditorContext* context = m_context;
                    m_scenes->ForEachScene(
                        [&](scene::Scene& other)
                        {
                            if (&other == self)
                            {
                                return;
                            }
                            const u32 rebuilt = scene::RebuildPrefabInstances(
                                other, prefabId, Span<const byte>{bytes.Data(), bytes.Size()},
                                m_editContext->PrefabResolver());
                            if (rebuilt > 0 && context->Resources() != nullptr)
                            {
                                scene::ResolveSceneResources(other, *context->Resources());
                            }
                        });
                }
            }
        }
        return saved;
    }

    void SceneEditorPage::OnClose()
    {
        // A closing page must stop claiming clip opens (the interceptor holds a raw `this`).
        if (m_openAssetInterceptorId != 0)
        {
            m_context->RemoveOpenAssetInterceptor(m_openAssetInterceptorId);
            m_openAssetInterceptorId = 0;
        }
        m_camera.ReleaseCapture(m_viewport ? m_viewport->Mouse() : nullptr); // never close captured

        // GPU targets + external-texture registration go while device + VGRenderer live.
        m_viewport->Shutdown();
        if (m_scene != nullptr)
        {
            m_sceneManager.DestroyScene(m_scene); // aware subsystems get OnSceneDestroyed
            m_scene = nullptr;
        }
        if (m_scenes != nullptr)
        {
            m_scenes->UnregisterManager(&m_sceneManager);
        }
    }

    bool SceneEditorPage::MakeMouseRay(GizmoRay& out) const
    {
        foundation::shell::IMouse* mouse = m_viewport->Mouse();
        const u32 w = m_viewport->RenderWidth();
        const u32 h = m_viewport->RenderHeight();
        if (mouse == nullptr || w == 0 || h == 0)
        {
            return false;
        }
        const f32 ndcX = 2.0f * (mouse->X() / static_cast<f32>(w)) - 1.0f;
        const f32 ndcY = 1.0f - 2.0f * (mouse->Y() / static_cast<f32>(h));
        const f32 tanY = Tan(kFovY * 0.5f);
        const f32 tanX = tanY * (static_cast<f32>(w) / static_cast<f32>(h));
        out.origin = m_camera.position;
        out.direction = Normalized(m_camera.Forward() + m_camera.Right() * (ndcX * tanX) +
                                   m_camera.Up() * (ndcY * tanY));
        return true;
    }

    void SceneEditorPage::BuildToolFloat()
    {
        // The toolkit FloatingPanel owns drag / resize / collapse / close (and clamps itself to the
        // viewport frame). Closing it deactivates the active tool, which unmounts the panel via the
        // tool-panel host's clear callback - so close reads as "put the brush away".
        m_toolFloat = MakeRef<ui::toolkit::FloatingPanel>(DefaultAllocator(), StringView(u8"Brush"));
        m_toolFloat->Visibility = ui::Visibility::Gone;
        SceneEditorPage* page = this;
        m_toolFloat->OnClose.Add([page]() { page->m_viewportTools.ActivateDefault(); });
    }

    void SceneEditorPage::MountToolPanel(foundation::ui::View* view, ToolPanelPlacement placement)
    {
        // Route by the provider's placement hint. Dock is implemented (the "Brush" bottom-dock tab);
        // Float / ViewportOverlay are experiment slots - until they have a real presentation they fall
        // back to the dock, so a provider can opt into them without the panel silently vanishing.
        switch (placement)
        {
        case ToolPanelPlacement::ViewportOverlay:
        {
            // A themed HUD floating over the viewport - eyes stay on the terrain while brushing.
            foundation::ui::Panel* overlay = m_toolOverlay.Get();
            if (overlay == nullptr)
            {
                return;
            }
            while (overlay->ChildCount() > 0)
            {
                overlay->RemoveView(overlay->GetChildAt(0), true);
            }
            if (view != nullptr)
            {
                overlay->AddView(view, MakeRef<foundation::ui::LayoutParams>(DefaultAllocator()));
                overlay->Visibility = foundation::ui::Visibility::Visible;
            }
            else
            {
                overlay->Visibility = foundation::ui::Visibility::Gone;
            }
            break;
        }
        case ToolPanelPlacement::Float:
        {
            // A FloatingPanel over the viewport: drag / resize / collapse / close (see FloatingPanel).
            if (m_toolFloat.Get() == nullptr)
            {
                return;
            }
            m_toolFloat->SetContent(RefPtr<foundation::ui::View>(view));
            m_toolFloat->Visibility =
                (view != nullptr) ? foundation::ui::Visibility::Visible : foundation::ui::Visibility::Gone;
            break;
        }
        case ToolPanelPlacement::Dock:
        default:
        {
            foundation::ui::FlexLayout* slot = m_toolPanelSlot.Get();
            if (slot == nullptr)
            {
                return;
            }
            while (slot->ChildCount() > 0)
            {
                slot->RemoveView(slot->GetChildAt(0), true);
            }
            if (view != nullptr)
            {
                slot->AddView(view, MakeRef<foundation::ui::LayoutParams>(DefaultAllocator()));
                m_bottomDock->ActivateTab(u8"tool"); // reveal the brush settings
            }
            break;
        }
        }
    }

    bool SceneEditorPage::UpdateViewportTools(bool viewportActive, f32 deltaSeconds)
    {
        if (m_selectTool == nullptr)
        {
            return false;
        }
        ViewportToolInput in;
        in.deltaSeconds = deltaSeconds;
        in.cameraPosition = m_camera.position;
        in.cameraForward = m_camera.Forward();
        in.fovY = kFovY;
        // Simulate: edits are refused (the select tool maps this to its read-only gizmo path -
        // pose sync, no drags; drag commands are refused by the locked stack anyway). Selection
        // picking still works: selecting is not an edit.
        in.editingLocked = m_isSimulating;
        GizmoRay ray;
        in.pointerValid = viewportActive && MakeMouseRay(ray);
        in.ray.origin = ray.origin;
        in.ray.direction = ray.direction;
        in.pointerOver = m_viewport->IsHovered();
        if (!in.pointerValid)
        {
            return m_viewportTools.Update(in);
        }

        foundation::shell::IMouse* mouse = m_viewport->Mouse();
        foundation::shell::IKeyboard* kb = m_viewport->Keyboard();

        const bool cameraOwnsMouse =
            (kb != nullptr && (kb->IsKeyDown(foundation::shell::KeyCode::LeftAlt) ||
                               kb->IsKeyDown(foundation::shell::KeyCode::RightAlt))) ||
            mouse->IsButtonDown(foundation::shell::MouseButton::Right) ||
            m_camera.mouseCaptured; // Tab-captured fly mode owns WASD too
        if (!cameraOwnsMouse)
        {
            in.leftPressed = mouse->IsButtonPressed(foundation::shell::MouseButton::Left);
            in.leftDown = mouse->IsButtonDown(foundation::shell::MouseButton::Left);
        }
        // Release always reaches the tool so an in-flight gesture can finish even if a
        // modifier goes down mid-drag.
        in.leftReleased = mouse->IsButtonReleased(foundation::shell::MouseButton::Left) ||
                          !mouse->IsButtonDown(foundation::shell::MouseButton::Left);
        if (kb != nullptr)
        {
            in.ctrl = kb->IsKeyDown(foundation::shell::KeyCode::LeftCtrl) ||
                      kb->IsKeyDown(foundation::shell::KeyCode::RightCtrl);
            in.shift = kb->IsKeyDown(foundation::shell::KeyCode::LeftShift) ||
                       kb->IsKeyDown(foundation::shell::KeyCode::RightShift);
        }
        in.wheelDelta = mouse->ScrollY();
        // Tool hotkeys (the select tool's W/E/R/X) belong to the camera while it owns input.
        in.keyboard = cameraOwnsMouse ? nullptr : kb;
        return m_viewportTools.Update(in);
    }

    void SceneEditorPage::DrawGizmos(render::debug::DebugDraw& dd)
    {
        m_viewportTools.Draw(dd);
        if (IViewportTool* active = m_viewportTools.ActiveTool(); active != nullptr)
        {
            const StringView status = active->StatusText();
            if (!status.IsEmpty())
            {
                dd.DrawScreenText(12.0f, 12.0f, status, Color{0.85f, 0.85f, 0.85f, 1.0f});
            }
        }

        // Component gizmos: full set for the selected entity; opted-in renderers for the rest.
        if (!m_editContext)
        {
            return;
        }
        GizmoContext ctx;
        ctx.debug = &dd;
        ctx.scene = m_scene;
        ctx.cameraPosition = m_camera.position;
        // The viewport's camera for the LOD overlay's per-view pick - the same matrices the
        // render path builds (aspect from the live render target when it exists).
        render::ViewCamera gizmoCamera;
        gizmoCamera.view = Float4x4::LookAtRH(m_camera.position,
                                              m_camera.position + m_camera.Forward(), m_camera.Up());
        const f32 gizmoAspect =
            (m_viewport && m_viewport->IsReady() && m_viewport->RenderHeight() > 0)
                ? static_cast<f32>(m_viewport->RenderWidth()) /
                      static_cast<f32>(m_viewport->RenderHeight())
                : 16.0f / 9.0f;
        gizmoCamera.projection = Float4x4::PerspectiveFovRH(1.0472f, gizmoAspect, 0.1f, 1000.0f);
        gizmoCamera.position = m_camera.position;
        ctx.viewCamera = &gizmoCamera;
        ctx.lodOverlay = m_showLodOverlay;
        Selection<Guid>& selection = m_editContext->EntitySelection();
        m_scene->ForEachEntity(
            [&](scene::EntityHandle e)
            { m_componentGizmos.DrawEntity(e, selection.Contains(m_scene->GetEntityId(e)), ctx); });
    }

    void SceneEditorPage::ShowPostFlagsMenu(foundation::ui::View* anchor)
    {
        if (anchor == nullptr)
        {
            return;
        }
        auto menu = MakeRef<foundation::ui::ContextMenu>(DefaultAllocator());
        SceneEditorPage* self = this;
        const auto mark = [](bool on) { return on ? StringView(u8"[x] ") : StringView(u8"[ ] "); };
        const auto add = [&](StringView label, bool render::ViewPostOverride::* field)
        {
            String text(mark(m_postOverride.*field));
            text += label;
            menu->AddItem(text.AsView(), [self, field]()
                          { self->m_postOverride.*field = !(self->m_postOverride.*field); });
        };
        add(u8"No Post (bloom/AO/SSR/AA off)", &render::ViewPostOverride::disablePost);
        menu->AddSeparator();
        add(u8"No Bloom", &render::ViewPostOverride::disableBloom);
        add(u8"No AO", &render::ViewPostOverride::disableAo);
        add(u8"No SSR", &render::ViewPostOverride::disableSsr);
        add(u8"No AA (crisp)", &render::ViewPostOverride::disableAa);
        // Scene-pass MSAA (msaa.md): an INDEPENDENT off/2x/4x tri-state (not a bool - it forces the
        // view's sample count). The count is capability-clamped by the render subsystem, so a 4x pick
        // on a 2x device renders 2x. MSAA and TAA are independent (Decision 5); both can be on.
        menu->AddSeparator();
        const auto msaaItem = [&](StringView label, u8 count)
        {
            const bool on = (count <= 1) ? (self->m_postOverride.msaaOverride <= 1)
                                         : (self->m_postOverride.msaaOverride == count);
            String text(mark(on));
            text += label;
            menu->AddItem(text.AsView(),
                          [self, count]() { self->m_postOverride.msaaOverride = count; });
        };
        // Levels from engine::render::kMsaaLevels (the single source of truth; add 8x there once).
        for (u32 i = 0; i < engine::render::MsaaLevelCount(); ++i)
        {
            String label(u8"MSAA ");
            label += engine::render::kMsaaLevels[i].label;
            msaaItem(label.AsView(), static_cast<u8>(engine::render::kMsaaLevels[i].samples));
        }
        const Float2 pos = anchor->LocalToScreen(Float2{0.0f, anchor->Height()});
        menu->Show(anchor->Context, pos.x, pos.y);
    }

    void SceneEditorPage::BuildViewportToolbar()
    {
        m_toolbar = MakeRef<ui::toolkit::Toolbar>(DefaultAllocator());
        editor::app::EditorIcons& icons = editor::app::EditorIcons::Get();
        GizmoController* gizmos = m_selectTool != nullptr ? &m_selectTool->Gizmos() : nullptr;
        auto icon = [](foundation::ui::SVGDrawable* drawable)
        {
            return Function<void(foundation::ui::UIDrawContext&, Rectangle)>{
                [drawable](foundation::ui::UIDrawContext& ctx, Rectangle rect)
                {
                    if (drawable != nullptr)
                    {
                        drawable->Draw(ctx, rect);
                    }
                }};
        };

        m_translateToggle = m_toolbar->AddToggle(u8"");
        m_translateToggle->SetIcon(icon(icons.translate.Get()));
        m_translateToggle->OnCheckedChanged.Add(
            [gizmos](ui::toolkit::ToolbarToggle*, bool value)
            {
                if (value)
                {
                    gizmos->SetMode(GizmoMode::Translate);
                }
            });
        m_rotateToggle = m_toolbar->AddToggle(u8"");
        m_rotateToggle->SetIcon(icon(icons.rotate.Get()));
        m_rotateToggle->OnCheckedChanged.Add(
            [gizmos](ui::toolkit::ToolbarToggle*, bool value)
            {
                if (value)
                {
                    gizmos->SetMode(GizmoMode::Rotate);
                }
            });
        m_scaleToggle = m_toolbar->AddToggle(u8"");
        m_scaleToggle->SetIcon(icon(icons.scale.Get()));
        m_scaleToggle->OnCheckedChanged.Add(
            [gizmos](ui::toolkit::ToolbarToggle*, bool value)
            {
                if (value)
                {
                    gizmos->SetMode(GizmoMode::Scale);
                }
            });

        m_toolbar->AddSeparator();

        // One toggle whose icon + label read the LIVE space (checked = world).
        m_spaceToggle = m_toolbar->AddToggle(u8"World");
        m_spaceToggle->SetIcon(Function<void(foundation::ui::UIDrawContext&, Rectangle)>{
            [gizmos, &icons](foundation::ui::UIDrawContext& ctx, Rectangle rect)
            {
                foundation::ui::SVGDrawable* drawable = (gizmos->Space() == GizmoSpace::World)
                                                          ? icons.worldSpace.Get()
                                                          : icons.localSpace.Get();
                if (drawable != nullptr)
                {
                    drawable->Draw(ctx, rect);
                }
            }});
        m_spaceToggle->OnCheckedChanged.Add(
            [gizmos](ui::toolkit::ToolbarToggle* toggle, bool value)
            {
                gizmos->SetSpace(value ? GizmoSpace::World : GizmoSpace::Local);
                toggle->SetText(value ? StringView(u8"World") : StringView(u8"Local"));
            });

        m_toolbar->AddSeparator();

        ScenePage_GridToggleInit();

        // Post show-flags: ephemeral per-view overrides that strip effects for editing clarity
        // (never written to the scene). A "Post" button opens a checkable menu.
        {
            SceneEditorPage* self = this;
            ui::toolkit::ToolbarButton* postButton = m_toolbar->AddButton(u8"Post");
            postButton->OnClick.Add([self](ui::toolkit::ToolbarButton* btn)
                                    { self->ShowPostFlagsMenu(btn); });
        }

        // Viewport tool palette (APPENDED after the built-ins so the fixed toolbar shape never
        // shifts as tool plugins come and go): a toggle per non-default registered tool (index 0 is
        // the default Select/gizmo tool, driven by the gizmo toggles). Checking one activates that
        // tool - which docks its panel (terrain brushes, future nav-mesh); unchecking (or checking
        // another) returns to the default. This is the entry point to the in-scene modes.
        if (m_viewportTools.Count() > 1)
        {
            m_toolbar->AddSeparator();
            for (usize i = 1; i < m_viewportTools.Count(); ++i)
            {
                IViewportTool* tool = m_viewportTools.ToolAt(i);
                if (tool == nullptr)
                {
                    continue;
                }
                String id(tool->Id());
                ui::toolkit::ToolbarToggle* toggle = m_toolbar->AddToggle(tool->DisplayName());
                toggle->OnCheckedChanged.Add(
                    [this, id](ui::toolkit::ToolbarToggle*, bool value)
                    {
                        if (value)
                        {
                            m_viewportTools.ActivateById(id.AsView());
                        }
                        else if (m_viewportTools.ActiveTool() != nullptr &&
                                 m_viewportTools.ActiveTool()->Id() == id.AsView())
                        {
                            m_viewportTools.ActivateDefault();
                        }
                        SyncToolbar();
                    });
                m_toolToggles.PushBack(ToolToggle{toggle, Move(id)});
            }
        }

        // Spacer pushes the simulation cluster to the right edge (Sedulous toolbar shape).
        {
            auto spacer = MakeRef<foundation::ui::Panel>(DefaultAllocator());
            auto lp = MakeRef<foundation::ui::FlexLayoutParams>(DefaultAllocator());
            lp->Grow = 1.0f;
            m_toolbar->AddView(spacer.Get(), lp);
        }

        // === Simulate (snapshot -> run -> restore; phase-8a half of play-in-editor) ===
        SceneEditorPage* self = this;
        m_playButton = m_toolbar->AddButton(u8"Play");
        m_playButton->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->StartSimulation(); });
        m_pauseToggle = m_toolbar->AddToggle(u8"Pause");
        m_pauseToggle->OnCheckedChanged.Add([self](ui::toolkit::ToolbarToggle*, bool value)
                                            { self->PauseSimulation(value); });
        m_stopButton = m_toolbar->AddButton(u8"Stop");
        m_stopButton->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->StopSimulation(); });
        // The at-a-glance state readout (user report: Play gave no visual indication).
        m_simLabel = MakeRef<foundation::ui::Label>(DefaultAllocator(), StringView(u8""));
        m_simLabel->FontSize.SetValue(13.0f);
        {
            auto lp = MakeRef<foundation::ui::FlexLayoutParams>(DefaultAllocator());
            lp->Height = foundation::ui::SizeSpec::Match();
            m_toolbar->AddView(m_simLabel.Get(), lp);
        }
        RefreshSimToolbar();
    }

    void SceneEditorPage::StartSimulation()
    {
        if (m_isSimulating || m_scene == nullptr)
        {
            return;
        }
        m_simSnapshot = scene::SceneSnapshot::Capture(*m_scene);
        if (!m_simSnapshot)
        {
            LOG_ERROR(u8"Editor", u8"Simulate: scene snapshot capture failed");
            return;
        }
        m_scene->Start();
        m_scene->SetSimulationEnabled(true);
        Commands().SetLocked(true);
        // Modal edit tools (terrain sculpt) share runtime state with the simulation (the collider):
        // drop back to the default select tool for the duration, and sync the palette toggles.
        m_viewportTools.ActivateDefault();
        SyncToolbar();
        m_isSimulating = true;
        m_isPaused = false;
        RefreshSimToolbar();
    }

    void SceneEditorPage::PauseSimulation(bool paused)
    {
        if (!m_isSimulating || m_scene == nullptr)
        {
            return;
        }
        m_isPaused = paused;
        m_scene->SetSimulationEnabled(!paused);
        RefreshSimToolbar();
    }

    void SceneEditorPage::StopSimulation()
    {
        if (!m_isSimulating || m_scene == nullptr)
        {
            return;
        }
        m_scene->Stop();
        if (m_simSnapshot)
        {
            foundation::resource::ResourceManager* resources =
                (m_context != nullptr) ? m_context->Resources() : nullptr;
            if (!m_simSnapshot->Restore(*m_scene, resources).IsOk())
            {
                LOG_ERROR(u8"Editor", u8"Simulate: snapshot restore failed");
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

    void SceneEditorPage::RefreshSimToolbar()
    {
        if (m_playButton == nullptr)
        {
            return;
        }
        m_playButton->IsEnabled = !m_isSimulating;
        m_pauseToggle->IsEnabled = m_isSimulating;
        m_stopButton->IsEnabled = m_isSimulating;
        m_pauseToggle->SetIsChecked(m_isPaused);
        if (m_simLabel.Get() != nullptr)
        {
            if (!m_isSimulating)
            {
                m_simLabel->SetText(u8"");
            }
            else
            {
                m_simLabel->SetText(m_isPaused ? StringView(u8" PAUSED ")
                                               : StringView(u8" SIMULATING "));
                m_simLabel->TextColor.SetValue(
                    Optional<Color>(m_isPaused ? Color{0.95f, 0.85f, 0.4f, 1.0f}     // amber
                                               : Color{0.95f, 0.55f, 0.35f, 1.0f})); // orange
            }
        }
        m_playButton->Invalidate();
        m_pauseToggle->Invalidate();
        m_stopButton->Invalidate();
    }

    void SceneEditorPage::ScenePage_GridToggleInit()
    {
        m_gridToggle = m_toolbar->AddToggle(u8"");
        m_gridToggle->SetIcon(Function<void(foundation::ui::UIDrawContext&, Rectangle)>{
            [](foundation::ui::UIDrawContext& ctx, Rectangle rect)
            {
                if (auto* drawable = editor::app::EditorIcons::Get().grid.Get())
                {
                    drawable->Draw(ctx, rect);
                }
            }});
        SceneEditorPage* self = this;
        m_gridToggle->OnCheckedChanged.Add([self](ui::toolkit::ToolbarToggle*, bool value)
                                           { self->m_showGrid = value; });
        // LOD overlay (mesh-lod.md P3): tint every chained mesh's bounds by the level this
        // viewport's camera selects. Off by default - a debug lens, not an editing mode.
        m_lodToggle = m_toolbar->AddToggle(u8"LOD");
        m_lodToggle->OnCheckedChanged.Add([self](ui::toolkit::ToolbarToggle*, bool value)
                                          { self->m_showLodOverlay = value; });
    }

    void SceneEditorPage::SyncToolbar()
    {
        if (m_toolbar.Get() == nullptr || m_selectTool == nullptr)
        {
            return;
        }
        const GizmoMode mode = m_selectTool->Gizmos().Mode();
        m_translateToggle->SetIsChecked(mode == GizmoMode::Translate);
        m_rotateToggle->SetIsChecked(mode == GizmoMode::Rotate);
        m_scaleToggle->SetIsChecked(mode == GizmoMode::Scale);
        const bool world = (m_selectTool->Gizmos().Space() == GizmoSpace::World);
        m_spaceToggle->SetIsChecked(world);
        m_gridToggle->SetIsChecked(m_showGrid);
        if (m_lodToggle != nullptr)
        {
            m_lodToggle->SetIsChecked(m_showLodOverlay);
        }

        IViewportTool* activeTool = m_viewportTools.ActiveTool();
        const StringView activeId = activeTool != nullptr ? activeTool->Id() : StringView{};
        for (const ToolToggle& tt : m_toolToggles)
        {
            if (tt.toggle != nullptr)
            {
                tt.toggle->SetIsChecked(tt.id.AsView() == activeId);
            }
        }
    }

    void SceneEditorPage::DrawEntityMarkers(render::debug::DebugDraw& dd)
    {
        if (!m_editContext)
        {
            return;
        }
        Selection<Guid>& selection = m_editContext->EntitySelection();
        scene::Scene& scene = *m_scene;
        auto* meshes = scene.GetSystem<engine::render::MeshComponentManager>();
        auto* instanced = scene.GetSystem<engine::render::InstancedMeshComponentManager>();
        scene.ForEachEntity(
            [&](scene::EntityHandle e)
            {
                const Float4x4 world = scene.GetWorldMatrix(e);
                const Float3 p{world.m[3][0], world.m[3][1], world.m[3][2]};
                const bool selected = selection.Contains(scene.GetEntityId(e));
                const f32 s = 0.25f;
                const Color color =
                    selected ? Color{1.0f, 0.85f, 0.25f, 1.0f} : Color{0.75f, 0.75f, 0.80f, 1.0f};
                dd.DrawLine(p - Float3{s, 0, 0}, p + Float3{s, 0, 0}, color);
                dd.DrawLine(p - Float3{0, s, 0}, p + Float3{0, s, 0}, color);
                dd.DrawLine(p - Float3{0, 0, s}, p + Float3{0, 0, s}, color);
                if (!selected)
                {
                    return;
                }

                if (meshes != nullptr)
                {
                    if (engine::render::MeshComponent* mc = meshes->Get(e))
                    {
                        if (foundation::geometry::StaticMesh* mesh = mc->mesh.Get())
                        {
                            dd.DrawTransformedBox(mesh->bounds.min, mesh->bounds.max, world, color);
                            return;
                        }
                    }
                }
                if (instanced != nullptr)
                {
                    if (engine::render::InstancedMeshComponent* imc = instanced->Get(e))
                    {
                        if (imc->mesh.Get() != nullptr && imc->Count() > 0 &&
                            imc->cachedRadius > 0.0f)
                        {
                            // Merged world bounds (kept current by extraction's compose pass).
                            const f32 r = imc->cachedRadius;
                            dd.DrawWireBoxCenter(imc->cachedCenter, Float3{r, r, r}, color);
                            return;
                        }
                    }
                }
                dd.DrawWireBoxCenter(p, Float3{0.35f, 0.35f, 0.35f}, color);
            });
    }

    // === Camera preview (task #118) ===

    void SceneEditorPage::BuildCameraPreview()
    {
        // Display-only viewport (input=nullptr at Initialize): it shows the previewed camera's
        // view and never takes hover/focus/pick from the main viewport. Fixed 16:9 resolution so
        // the aspect stays stable regardless of the floating panel's laid-out size.
        m_previewViewport = MakeRef<ui::viewport::ViewportView>(DefaultAllocator());
        m_previewViewport->ClearColor = rhi::ClearColor{0.0f, 0.0f, 0.0f, 1.0f};
        m_previewViewport->SetFixedResolution(320, m_previewHeight);

        m_previewPin = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Pin"));
        {
            SceneEditorPage* self = this;
            m_previewPin->OnClick.Add([self](ui::ButtonBase*) { self->ToggleCameraPin(); });
        }

        auto container = MakeRef<ui::FlexLayout>(DefaultAllocator());
        container->Direction = ui::Orientation::Vertical;
        container->Visibility = ui::Visibility::Gone; // idle until a camera is previewed
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(24.0f));
            container->AddView(m_previewPin.Get(), lp);
        }
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Fixed(ui::Unit::Dp(320.0f));
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Dp(static_cast<f32>(m_previewHeight)));
            container->AddView(m_previewViewport.Get(), lp);
        }
        m_previewContainer = container;
    }

    scene::EntityHandle SceneEditorPage::SelectedCameraEntity() const
    {
        if (!m_editContext || m_scene == nullptr)
        {
            return scene::EntityHandle{};
        }
        const Guid* primary = m_editContext->EntitySelection().Primary();
        if (primary == nullptr)
        {
            return scene::EntityHandle{};
        }
        const scene::EntityHandle e = m_editContext->Resolve(*primary);
        return IsLiveCamera(e) ? e : scene::EntityHandle{};
    }

    bool SceneEditorPage::IsLiveCamera(scene::EntityHandle entity) const
    {
        if (m_scene == nullptr || !entity.IsAssigned() || !m_scene->IsValid(entity))
        {
            return false;
        }
        auto* cameras = m_scene->GetSystem<engine::render::CameraComponentManager>();
        return cameras != nullptr && cameras->Get(entity) != nullptr;
    }

    void SceneEditorPage::UpdateCameraPreview()
    {
        if (!m_previewContainer)
        {
            return;
        }
        // SelectedCameraEntity() returns unassigned for a non-camera selection, so
        // selection.IsAssigned() IS "a camera is selected" for the resolver.
        const scene::EntityHandle selection = SelectedCameraEntity();
        const CameraPreviewResolution res = ResolveCameraPreview(
            selection, selection.IsAssigned(), m_pinnedCamera, IsLiveCamera(m_pinnedCamera));
        if (res.unpin)
        {
            m_pinnedCamera = scene::EntityHandle{}; // a stale pin auto-clears
        }
        m_previewTarget = res.target;

        const ui::Visibility vis = res.visible ? ui::Visibility::Visible : ui::Visibility::Gone;
        if (m_previewContainer->Visibility != vis)
        {
            m_previewContainer->Visibility = vis;
            m_previewContainer->Invalidate(); // visibility flip only - no view churn
        }
        if (m_previewPin)
        {
            const bool pinned =
                m_pinnedCamera.IsAssigned() && m_previewTarget == m_pinnedCamera;
            m_previewPin->SetText(pinned ? StringView(u8"Unpin") : StringView(u8"Pin"));
        }
    }

    void SceneEditorPage::ToggleCameraPin()
    {
        if (m_pinnedCamera.IsAssigned() && m_pinnedCamera == m_previewTarget)
        {
            m_pinnedCamera = scene::EntityHandle{}; // unpin
        }
        else if (m_previewTarget.IsAssigned())
        {
            m_pinnedCamera = m_previewTarget; // pin the currently-previewed camera
        }
        UpdateCameraPreview();
    }

    void SceneEditorPage::RenderCameraPreview()
    {
        if (!m_previewViewport || !m_previewTarget.IsAssigned())
        {
            return;
        }
        if (!m_previewViewport->IsReady() || m_render == nullptr || !m_render->IsReady() ||
            m_scene == nullptr || !m_previewViewport->IsEffectivelyVisible())
        {
            return;
        }
        const u32 w = m_previewViewport->RenderWidth();
        const u32 h = m_previewViewport->RenderHeight();
        if (w == 0 || h == 0)
        {
            return;
        }
        auto* cameras = m_scene->GetSystem<engine::render::CameraComponentManager>();
        if (cameras == nullptr)
        {
            return;
        }
        engine::render::CameraComponent* cam = cameras->Get(m_previewTarget);
        if (cam == nullptr)
        {
            return;
        }

        const Float4x4 world = m_scene->GetWorldMatrix(m_previewTarget);
        render::CameraOverride camOverride = BuildCameraPreviewOverride(*cam, world);

        render::TargetState targetState;
        targetState.texture = m_previewViewport->ColorTexture();
        targetState.currentState = m_previewViewport->ColorState();
        targetState.finalState = rhi::ResourceState::ShaderRead;

        // Keyed by the PREVIEW viewport: its own (empty) debug list, so the editor's grid/gizmos -
        // written to DebugView(m_viewport) - never appear here. This is the whole fix (task #118).
        m_render->RenderScene(*m_scene, m_previewViewport->ColorTargetView(),
                              m_previewViewport->ColorFormat(), w, h,
                              render::ViewportRect{0, 0, w, h}, &camOverride, targetState,
                              /*postOverride*/ nullptr, /*viewportKey*/ m_previewViewport.Get());
        m_previewViewport->SetColorState(rhi::ResourceState::ShaderRead);
    }

    void SceneEditorPage::EnsureViewportBound()
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
        } // float's AttachWindow hasn't run yet

        if (m_hostWindow == nullptr)
        {
            m_viewport->Initialize(m_host->Graphics()->Raw(), renderer, m_host->Shell()->Input(),
                                   window->Window().Id());
            if (m_viewport->Surface() != nullptr)
            {
                m_router->AddSurface(m_viewport->Surface());
            }
            // Display-only (input=nullptr): the preview never takes hover/focus/pick, it just
            // shows the previewed camera's view. Same window + renderer as the main viewport.
            if (m_previewViewport)
            {
                m_previewViewport->Initialize(m_host->Graphics()->Raw(), renderer, nullptr,
                                              window->Window().Id());
            }
        }
        else
        {
            m_viewport->AttachToWindow(renderer, window->Window().Id());
            if (m_previewViewport)
            {
                m_previewViewport->AttachToWindow(renderer, window->Window().Id());
            }
        }
        m_hostWindow = window;
    }
    const TypeInfo* SceneEditorPageFactory::PrimaryType() const
    {
        return &scene::SceneDocument::StaticType();
    }

    UniquePtr<EditorPage> SceneEditorPageFactory::CreatePage(EditorContext& context,
                                                             foundation::content::Instance& instance)
    {
        return UniquePtr<EditorPage>(
            DefaultAllocator().New<SceneEditorPage>(context, *m_host, *m_uiHost, instance),
            DefaultAllocator());
    }
}
