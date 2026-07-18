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
import draconic.shell;
import draconic.input;
import draconic.input.resource;
import draconic.input.subsystem;
import draconic.physics.subsystem;
import draconic.runtime.defaultapp;
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

    // The play-in-editor device seam (input P3): keyboard/mouse come from the Game
    // viewport's GATED InputSurface facades (hover = mouse, focus = keyboard - click the
    // viewport to play), gamepads pass through from the shell only while the viewport has
    // focus. This is the by-construction fix for "the editor viewport forwards nothing".
    // Pure forwarding: EVERY facade comes from the viewport's InputSurface, which owns all
    // gating and transformation (mouse hover+transform, keyboard focus, gamepads gated on
    // focus via SurfaceGamepad, touch transformed + spatially gated). This class only
    // adapts the surface to the input runtime's provider seam - no gating logic here.
    class GameViewportInputSource final : public draconic::input::IInputSourceProvider
    {
    public:
        guivp::ViewportView* viewport = nullptr;          // borrowed
        draconic::shell::IInputManager* shellInput = nullptr;   // borrowed (count only)

        [[nodiscard]] draconic::shell::IKeyboard* Keyboard() override
        {
            return viewport != nullptr ? viewport->Keyboard() : nullptr;
        }
        [[nodiscard]] draconic::shell::IMouse* Mouse() override
        {
            return viewport != nullptr ? viewport->Mouse() : nullptr;
        }
        [[nodiscard]] i32 GamepadCount() const override
        {
            // Count is structural; the surface's per-pad facades gate the actual reads.
            return shellInput != nullptr ? Min(shellInput->GamepadCount(), 8) : 0;
        }
        [[nodiscard]] draconic::shell::IGamepad* Gamepad(i32 index) override
        {
            auto* surface = viewport != nullptr ? viewport->Surface() : nullptr;
            return surface != nullptr ? surface->Gamepad(index) : nullptr;
        }
        [[nodiscard]] draconic::shell::ITouch* Touch() override
        {
            return viewport != nullptr ? viewport->Touch() : nullptr;
        }
    };

    // Wren runtime faults during play surface as editor notices, not console-only lines.
    class GameScriptErrorSink final : public draconic::script::IScriptErrorHandler
    {
    public:
        EditorContext* context = nullptr;
        void OnError(const draconic::script::ScriptError& error) override
        {
            if (context == nullptr) { return; }
            String message(u8"Game script error: ");
            message += error.message;
            context->Notify(NoticeKind::Error, message.AsView());
        }
    };

    class GameEditorPage final : public app::UIEditorPage
    {
    public:
        GameEditorPage(EditorContext& context, grt::IApplicationHost& host,
                       grt::DefaultApplication* embeddedApp)
            : m_context(&context), m_host(&host), m_app(embeddedApp)
        {
            m_scenes = host.Ctx().GetSubsystem<gscene::SceneSubsystem>();
            m_render = host.Ctx().GetSubsystem<grender::RenderSubsystem>();
            m_input = host.Ctx().GetSubsystem<draconic::input::InputSubsystem>();
            m_shellInput = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;

            m_viewport = MakeRef<guivp::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.05f, 0.05f, 0.06f, 1.0f };

            // The viewport's gated facades are the runtime InputSubsystem's PERMANENT
            // source (v3): the runtime context's input serves ONLY the game, so there is
            // nothing to swap back to on Stop.
            m_viewportSource.viewport = m_viewport.Get();
            m_viewportSource.shellInput = m_shellInput;
            if (m_input != nullptr) { m_input->SetSourceProvider(&m_viewportSource); }

            // "Exit" from embedded game code = stop this play session (deferred by the
            // app to after the page-update loop - never torn down mid-script-dispatch).
            context.StopGameRun = Function<void()>{ [this]() { Stop(); } };

            // Toolbar: Play / Stop / Restart + the run-state readout.
            GameEditorPage* self = this;
            m_toolbar = MakeRef<gtk::Toolbar>(DefaultAllocator());
            m_playButton = m_toolbar->AddButton(u8"Play");
            m_playButton->OnClick.Add([self](gtk::ToolbarButton*) { self->Play(); });
            m_pauseToggle = m_toolbar->AddToggle(u8"Pause");
            m_pauseToggle->OnCheckedChanged.Add([self](gtk::ToolbarToggle*, bool paused) {
                if (self->m_scene != nullptr && self->m_running)
                {
                    self->m_scene->SetSimulationEnabled(!paused);
                }
            });
            m_stopButton = m_toolbar->AddButton(u8"Stop");
            m_stopButton->OnClick.Add([self](gtk::ToolbarButton*) { self->Stop(); });
            m_restartButton = m_toolbar->AddButton(u8"Restart");
            m_restartButton->OnClick.Add([self](gtk::ToolbarButton*) {
                self->Stop();
                self->Play();
            });
            // Preview resolution: Auto (panel size) / Deck 1280x800 / 1080p - letterboxed,
            // with mouse AND touch input mapping through the same fit.
            m_resolutionButton = m_toolbar->AddButton(u8"Res: Auto");
            m_resolutionButton->OnClick.Add([self](gtk::ToolbarButton*) { self->CycleResolution(); });
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

            // Nudge a background incremental cook so just-edited content is fresh; the
            // run starts immediately and late products heal via the hot-reload path.
            if (m_context->OnCookRequested) { m_context->OnCookRequested(false); }
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
            if (m_pauseToggle != nullptr) { m_pauseToggle->SetIsChecked(false); }
            m_sceneTitle = String(instance->Name());
            BindInput();
            // The play bracket + game script are the EMBEDDED APP's (same lifecycle as
            // the standalone player); the page only resolves the script SOURCE (editor
            // project layout) and surfaces notices.
            if (m_app != nullptr)
            {
                m_app->SetPrimaryScene(m_scene);
                m_app->OnLaunch(*m_host);
                m_scriptErrors.context = m_context;
                m_app->SetGameScriptErrorHandler(&m_scriptErrors);
                StartGameScriptFromProject();
            }
            DRACONIC_LOG_INFO(u8"Editor", u8"Game: running scene '{}'", m_sceneTitle);
            RefreshToolbar();
        }

        /// Total teardown - the fresh-run model's whole cleanup story.
        void Stop()
        {
            if (!m_running && m_scene == nullptr) { return; }
            // The map clears (no actions bound between runs); the SOURCE stays - it is
            // the runtime input's permanent provider (v3).
            if (m_input != nullptr) { m_input->SetMap(draconic::input::InputMap{}); }
            // Script exits first (it may still observe the world), then the scene.
            if (m_app != nullptr)
            {
                m_app->StopGameScript();
                m_app->SetGameScriptErrorHandler(nullptr);
                m_app->SetPrimaryScene(nullptr);
                m_app->OnExit(*m_host);
            }
            if (m_scene != nullptr)
            {
                m_scene->Stop();
                if (m_scenes != nullptr) { m_scenes->DestroyScene(m_scene); }
                m_scene = nullptr;
            }
            m_running = false;
            RefreshToolbar();
        }

        void OnUpdate(grt::IApplicationHost& host, f32 dt) override
        {
            m_viewport->SyncInputRegion();
            // The play bracket: the embedded app updates ONLY while a run is live (its
            // OnUpdate ticks the game script with the primary scene's scaled time).
            if (m_running && m_app != nullptr) { m_app->OnUpdate(host, dt); }
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
            m_context->StopGameRun = Function<void()>{};
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

        // Play-in-editor input: the project's default map into the shared InputSubsystem,
        // devices swapped to the Game viewport's gated facades. Stop restores the shell.
        void BindInput()
        {
            if (m_input == nullptr) { return; }
            m_viewportSource.viewport = m_viewport.Get();
            m_viewportSource.shellInput = m_shellInput;
            m_input->SetSourceProvider(&m_viewportSource);
            const Guid mapId = m_context->Project()->Settings().defaultInputMapId;
            if (mapId.IsNil() || m_context->Resources() == nullptr) { return; }
            auto proxy = m_context->Resources()->Bind<draconic::input::InputMapResource>(mapId);
            if (proxy)
            {
                m_input->SetMap(proxy->Map());
                DRACONIC_LOG_INFO(u8"Editor", u8"Game: input map bound ({} set(s))",
                                  proxy->Map().sets.Size());
            }
            else
            {
                m_context->Notify(NoticeKind::Warning,
                    u8"Game: default input map is not cooked yet.");
            }
        }

        // Resolves the startup script's SOURCE (editor project layout); the lifecycle -
        // facades, services, launch/update/exit, fault handling - is the embedded app's.
        void StartGameScriptFromProject()
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
            if (!m_app->StartGameScript(
                    StringView(reinterpret_cast<const utf8char*>(bytes.Data()), bytes.Size()),
                    scriptPath))
            {
                m_context->Notify(NoticeKind::Error,
                    u8"Game: startup script failed to start (see Console).");
            }
        }

        void CycleResolution()
        {
            m_resolutionMode = (m_resolutionMode + 1u) % 3u;
            switch (m_resolutionMode)
            {
                case 0u:
                    m_viewport->SetFixedResolution(0, 0);
                    m_viewport->SetFitMode(FitMode::Stretch);
                    m_resolutionButton->SetText(u8"Res: Auto");
                    break;
                case 1u:
                    m_viewport->SetFixedResolution(1280, 800);
                    m_viewport->SetFitMode(FitMode::Letterbox);
                    m_resolutionButton->SetText(u8"Res: 1280x800");
                    break;
                case 2u:
                    m_viewport->SetFixedResolution(1920, 1080);
                    m_viewport->SetFitMode(FitMode::Letterbox);
                    m_resolutionButton->SetText(u8"Res: 1920x1080");
                    break;
                default: break;
            }
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
        grt::IApplicationHost* m_host = nullptr;
        grt::DefaultApplication* m_app = nullptr;   // the embedded game application (v3)
        gscene::SceneSubsystem* m_scenes = nullptr;
        grender::RenderSubsystem* m_render = nullptr;
        gscene::Scene* m_scene = nullptr;
        draconic::input::InputSubsystem* m_input = nullptr;
        draconic::shell::IInputManager* m_shellInput = nullptr;
        GameViewportInputSource m_viewportSource;

        RefPtr<draconic::ui::View> m_content;
        RefPtr<gtk::Toolbar> m_toolbar;
        gtk::ToolbarButton* m_playButton = nullptr;
        gtk::ToolbarButton* m_stopButton = nullptr;
        gtk::ToolbarToggle* m_pauseToggle = nullptr;
        gtk::ToolbarButton* m_restartButton = nullptr;
        gtk::ToolbarButton* m_resolutionButton = nullptr;
        u32 m_resolutionMode = 0;
        GameScriptErrorSink m_scriptErrors;
        RefPtr<draconic::ui::Label> m_statusLabel;
        RefPtr<guivp::ViewportView> m_viewport;


        String m_sceneTitle;
        bool m_running = false;
    };
}
