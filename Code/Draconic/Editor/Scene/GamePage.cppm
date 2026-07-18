// Draconic::EditorScene - :game_page partition.
//
// GameEditorPage (play-in-editor phase 8b, docs/design/roadmap.md MVP item 4): a singleton
// "Game" dock tab hosting the PLAYER behavior - a FRESH run of the project's default scene,
// exactly what RaptorPlayer does, in-process. Distinct from the ScenePage's Simulate
// (in-place snapshot -> run -> restore): nothing here is edited, so Play builds everything
// from scratch (fresh Scene + resolve + Start) and Stop tears it all down - total cleanup IS
// the restore. Renders through the scene's own primary camera (RenderScene with no override;
// EnsureCamera frames the origin when the scene ships none, like the player).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module draconic.editor.scene:game_page;

import draconic.core;
import draconic.content;
import draconic.vfs;
import draconic.resource;
import draconic.scene;
import draconic.scene.resource;
import draconic.render.subsystem;
import draconic.runtime;
import draconic.runtime.client;
import draconic.graphics;
import draconic.scene.subsystem;
import draconic.rhi;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.ui.runtime;
import draconic.ui.viewport;
import draconic.script;
import draconic.script.wren;
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace grt = draconic::runtime;
    namespace guirt = draconic::ui::runtime;
    namespace guivp = draconic::ui::viewport;
    namespace gscene = draconic::scene;
    namespace grender = draconic::render;
    namespace gtk = draconic::ui::toolkit;

    class GameEditorPage final : public app::UIEditorPage
    {
    public:
        GameEditorPage(EditorContext& context, grt::IApplicationHost& host)
            : m_context(&context)
        {
            m_scenes = host.Ctx().GetSubsystem<gscene::SceneSubsystem>();
            m_render = host.Ctx().GetSubsystem<grender::RenderSubsystem>();

            m_viewport = MakeRef<guivp::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.05f, 0.05f, 0.06f, 1.0f };

            // Toolbar: Play / Stop / Restart + the run-state readout.
            GameEditorPage* self = this;
            m_toolbar = MakeRef<gtk::Toolbar>(DefaultAllocator());
            m_playButton = m_toolbar->AddButton(u8"Play");
            m_playButton->OnClick.Add([self](gtk::ToolbarButton*) { self->Play(); });
            m_stopButton = m_toolbar->AddButton(u8"Stop");
            m_stopButton->OnClick.Add([self](gtk::ToolbarButton*) { self->Stop(); });
            m_restartButton = m_toolbar->AddButton(u8"Restart");
            m_restartButton->OnClick.Add([self](gtk::ToolbarButton*) {
                self->Stop();
                self->Play();
            });
            m_statusLabel = MakeRef<draconic::ui::Label>(DefaultAllocator(), StringView(u8""));
            m_statusLabel->FontSize.SetValue(13.0f);
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Height = draconic::ui::SizeSpec::Match();
                m_toolbar->AddView(m_statusLabel.Get(), lp);
            }

            auto column = MakeRef<draconic::ui::FlexLayout>(DefaultAllocator());
            column->Direction = draconic::ui::Orientation::Vertical;
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                lp->Height = draconic::ui::SizeSpec::Fixed(draconic::ui::Unit::Px(30));
                column->AddView(m_toolbar.Get(), lp);
            }
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                column->AddView(m_viewport.Get(), lp);
            }
            m_content = column;
            RefreshToolbar();
        }

        [[nodiscard]] StringView Title() const override { return u8"Game"; }
        [[nodiscard]] Status Save() override { return Status{}; }   // nothing here is a document
        [[nodiscard]] draconic::ui::View* ContentView() override { return m_content.Get(); }

        /// Fresh player run: the project's default scene from the DBs, simulation on.
        void Play()
        {
            if (m_running) { return; }
            if (m_scenes == nullptr || m_context->Project() == nullptr) { return; }

            // Resolution order mirrors RaptorPlayer: the manifest's guid (authoritative,
            // rename-proof), then the path mirror.
            EditorProject& project = *m_context->Project();
            draconic::content::Instance* instance = nullptr;
            if (!project.Settings().defaultSceneId.IsNil())
            {
                instance = project.SourceDb().GetInstance(project.Settings().defaultSceneId);
            }
            if (instance == nullptr && !project.Settings().defaultScene.IsEmpty())
            {
                instance = project.SourceDb().GetInstance(project.Settings().defaultScene.AsView());
            }
            if (instance == nullptr)
            {
                m_context->Notify(NoticeKind::Warning,
                    u8"No default scene - set one in Project Settings before playing.");
                return;
            }

            m_scene = m_scenes->CreateScene(instance->Name());
            if (m_scene == nullptr || !gscene::LoadScene(*instance, *m_scene).IsOk())
            {
                m_context->Notify(NoticeKind::Error, u8"Game: default scene failed to load.");
                if (m_scene != nullptr) { m_scenes->DestroyScene(m_scene); m_scene = nullptr; }
                return;
            }
            // Products bind from the cooked DB (the editor's shared manager); prefab payloads
            // come from the source DB - the same split RaptorPlayer uses in project mode.
            if (m_context->Resources() != nullptr)
            {
                gscene::ResolveSceneResources(*m_scene, *m_context->Resources());
            }
            if (m_scene->PendingPrefabInstanceCount() > 0)
            {
                EditorContext* context = m_context;
                gscene::ResolveScenePrefabs(*m_scene,
                    Function<UniquePtr<IStream>(const Guid&)>{
                        [context](const Guid& prefabId) -> UniquePtr<IStream> {
                            if (context->Project() == nullptr) { return UniquePtr<IStream>{}; }
                            draconic::content::Instance* prefab =
                                context->Project()->SourceDb().GetInstance(prefabId);
                            return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                       : UniquePtr<IStream>{};
                        } });
                if (m_context->Resources() != nullptr)
                {
                    gscene::ResolveSceneResources(*m_scene, *m_context->Resources());
                }
            }
            EnsureCamera();

            m_scene->Start();
            m_scene->SetSimulationEnabled(true);
            m_running = true;
            m_sceneTitle = String(instance->Name());
            StartGameScript();
            DRACONIC_LOG_INFO(u8"Editor", u8"Game: running scene '{}'", m_sceneTitle);
            RefreshToolbar();
        }

        /// Total teardown - the fresh-run model's whole cleanup story.
        void Stop()
        {
            if (!m_running && m_scene == nullptr) { return; }
            // Script first (its exit() may still observe the world), then the scene.
            if (m_game.Get() != nullptr)
            {
                (void)m_game->Invoke(u8"exit", Span<Variant>{});
                m_game = nullptr;
            }
            m_scriptContext = nullptr;
            m_scriptManager = nullptr;
            if (m_scene != nullptr)
            {
                m_scene->Stop();
                if (m_scenes != nullptr) { m_scenes->DestroyScene(m_scene); }
                m_scene = nullptr;
            }
            m_running = false;
            RefreshToolbar();
        }

        void OnUpdate(grt::IApplicationHost&, f32 dt) override
        {
            m_viewport->SyncInputRegion();
            if (m_running && m_game.Get() != nullptr)
            {
                Variant dtArg = Variant::From(dt);
                if (auto result = m_game->Invoke(u8"update", Span<Variant>{ &dtArg, 1 });
                    !result.HasValue())
                {
                    DRACONIC_LOG_ERROR(u8"Editor",
                        u8"Game: script update() faulted - stopping script (run continues)");
                    m_game = nullptr;
                }
            }
        }

        void OnRenderWindow(grt::IApplicationHost&, draconic::graphics::FrameContext& frame) override
        {
            if (!m_viewport->IsReady() || !frame.valid) { return; }
            if (m_render == nullptr || !m_render->IsReady() || m_scene == nullptr) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0) { return; }
            if (!m_viewport->IsEffectivelyVisible()) { return; }

            // No camera override: the SCENE's primary camera drives the view (its clear
            // color included) - the player's presentation, not the editor's.
            grender::TargetState targetState;
            targetState.texture = m_viewport->ColorTexture();
            targetState.currentState = m_viewport->ColorState();
            targetState.finalState = rhi::ResourceState::ShaderRead;
            m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(),
                                  w, h, grender::ViewportRect{ 0, 0, w, h }, nullptr, targetState);
            m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
        }

        void OnClose() override
        {
            Stop();
            m_viewport->Shutdown();
        }

    private:
        // Authored game scenes should carry a camera; a bare scene shouldn't play as a black
        // screen - frame the origin like RaptorPlayer does.
        void EnsureCamera()
        {
            auto* cameras = m_scene->GetSystem<grender::CameraComponentManager>();
            if (cameras == nullptr) { return; }
            bool hasCamera = false;
            cameras->ForEach([&](grender::CameraComponent&, gscene::EntityHandle) { hasCamera = true; });
            if (hasCamera) { return; }

            DRACONIC_LOG_WARNING(u8"Editor", u8"Game: scene has no camera - adding a default one");
            const gscene::EntityHandle e = m_scene->CreateEntity(u8"PlayerCamera");
            Transform t;
            t.position = Float3{ 8.0f, 6.0f, 10.0f };
            t.rotation = Quaternion::FromAxisAngle(Float3{ 0, 1, 0 }, 0.675f)
                       * Quaternion::FromAxisAngle(Float3{ 1, 0, 0 }, -0.42f);
            m_scene->SetLocalTransform(e, t);
            cameras->Add(e);
        }

        // The project's game script (Wren `Game` class: construct new() + optional
        // launch/update(dt)/exit) - RaptorPlayer's exact contract, per run: Play compiles a
        // FRESH context, Stop tears it down. Faults disable the script, never the run.
        void StartGameScript()
        {
            const StringView scriptPath = m_context->Project()->Settings().startupScript.AsView();
            if (scriptPath.IsEmpty()) { return; }
            draconic::vfs::NativeFileSystem root(m_context->Project()->Directory());
            UniquePtr<IStream> stream = root.Open(scriptPath, FileMode::Read);
            if (!stream)
            {
                m_context->Notify(NoticeKind::Warning, u8"Game: startup script not found.");
                return;
            }
            Array<byte> bytes;
            bytes.Resize(static_cast<usize>(stream->Size()));
            if (stream->Read(bytes.Data(), bytes.Size()) != bytes.Size()) { return; }
            const StringView source(reinterpret_cast<const utf8char*>(bytes.Data()), bytes.Size());

            m_scriptManager = draconic::script::wren::CreateScriptManager();
            draconic::script::RegisterReflectedTypes(*m_scriptManager);
            m_scriptContext = m_scriptManager->CreateContext();
            if (!m_scriptContext->Load(source, scriptPath).IsOk())
            {
                m_context->Notify(NoticeKind::Error,
                    u8"Game: startup script failed to compile (see Console).");
                m_scriptContext = nullptr;
                m_scriptManager = nullptr;
                return;
            }
            m_game = m_scriptContext->CreateInstance(u8"Game", Span<Variant>{});
            if (m_game.Get() == nullptr)
            {
                m_context->Notify(NoticeKind::Warning,
                    u8"Game: startup script has no `Game` class (construct new()).");
                return;
            }
            (void)m_game->Invoke(u8"launch", Span<Variant>{});
            DRACONIC_LOG_INFO(u8"Editor", u8"Game: script '{}' launched", scriptPath);
        }

        void RefreshToolbar()
        {
            if (m_statusLabel.Get() != nullptr)
            {
                if (m_running)
                {
                    String s(u8"  Running: ");
                    s += m_sceneTitle;
                    m_statusLabel->SetText(s.AsView());
                }
                else
                {
                    m_statusLabel->SetText(u8"  Stopped");
                }
            }
        }

        EditorContext* m_context = nullptr;
        gscene::SceneSubsystem* m_scenes = nullptr;
        grender::RenderSubsystem* m_render = nullptr;
        gscene::Scene* m_scene = nullptr;

        RefPtr<draconic::ui::View> m_content;
        RefPtr<gtk::Toolbar> m_toolbar;
        gtk::ToolbarButton* m_playButton = nullptr;
        gtk::ToolbarButton* m_stopButton = nullptr;
        gtk::ToolbarButton* m_restartButton = nullptr;
        RefPtr<draconic::ui::Label> m_statusLabel;
        RefPtr<guivp::ViewportView> m_viewport;

        RefPtr<draconic::script::IScriptManager> m_scriptManager;
        RefPtr<draconic::script::IScriptContext> m_scriptContext;
        RefPtr<draconic::script::ScriptObject> m_game;

        String m_sceneTitle;
        bool m_running = false;
    };
}
