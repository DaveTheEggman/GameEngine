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
import draconic.vg.renderer;
import draconic.ui.viewport;
import draconic.script;
import draconic.script.resource;    // ScriptClass (the cooked game script, bound from the content DB)
import draconic.script.subsystem;   // ScriptSubsystem / ScriptRunHost (debugger wiring)
import draconic.shell;
import draconic.input;
import draconic.input.resource;
import draconic.input.subsystem;
import draconic.physics.subsystem;
import draconic.audio;
import draconic.audio.resource;
import draconic.audio.subsystem;
import draconic.runtime.defaultapp;
import draconic.runtime.gameinstance;   // GameInstance - this tab drives its OWN run (multi-instance PIE)
import draconic.editor.core;
import draconic.editor.app;

using namespace draconic::core;

export namespace draconic::editor
{
    namespace runtime = draconic::runtime;
    namespace ui = draconic::ui;
    namespace scene = draconic::scene;
    namespace render = draconic::render;
    namespace vg = draconic::vg;
    namespace script = draconic::script;

    // The debugger panel (script-debugger.md P1): a Break/Continue/StepInto/StepOver toolbar, a
    // call-stack list, and a locals tree with one level of lazy object expansion. It consumes
    // ONLY the neutral IScriptDebugger + the snapshot types - no in-process assumptions - so the
    // same panel would drive a remote debugger. It never mutates views mid-event-dispatch: the
    // Game page rebuilds it from OnUpdate (top level); a locals-expand click only flags dirty.
    class DebuggerPanel
    {
    public:
        DebuggerPanel()
        {
            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 4.0f;
            column->Padding = ui::Thickness{ 6, 6 };

            auto bar = MakeRef<ui::FlexLayout>(DefaultAllocator());
            bar->Direction = ui::Orientation::Horizontal;
            bar->Spacing = 4.0f;
            DebuggerPanel* self = this;
            AddToolButton(*bar, u8"Continue", [self]() { if (self->m_debugger) { self->m_debugger->Continue(); } });
            AddToolButton(*bar, u8"Step Into", [self]() { if (self->m_debugger) { self->m_debugger->StepInto(); } });
            AddToolButton(*bar, u8"Step Over", [self]() { if (self->m_debugger) { self->m_debugger->StepOver(); } });
            AddToolButton(*bar, u8"Break", [self]() { if (self->m_debugger) { self->m_debugger->Break(); } });
            column->AddView(bar.Get(), MatchWidth());

            m_status = MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Debugger: not running"));
            m_status->FontSize.SetValue(12.0f);
            column->AddView(m_status.Get(), MatchWidth());

            column->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Call Stack")).Get(),
                            MatchWidth());
            m_stackList = MakeRef<ui::FlexLayout>(DefaultAllocator());
            m_stackList->Direction = ui::Orientation::Vertical;
            column->AddView(m_stackList.Get(), MatchWidth());

            column->AddView(MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Locals")).Get(),
                            MatchWidth());
            m_localsList = MakeRef<ui::FlexLayout>(DefaultAllocator());
            m_localsList->Direction = ui::Orientation::Vertical;
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Width = ui::SizeSpec::Match();
                column->AddView(m_localsList.Get(), lp);
            }
            m_root = column;
        }

        [[nodiscard]] ui::View* RootView() const noexcept { return m_root.Get(); }

        /// The active debugger for the run (null when not debugging). Clears the panel.
        void SetDebugger(script::IScriptDebugger* debugger)
        {
            m_debugger = debugger;
            m_expanded.Clear();
            if (debugger == nullptr) { Clear(); }
        }

        /// Rebuild the stack + locals from the debugger (called at a break, from OnUpdate).
        void Refresh()
        {
            if (m_debugger == nullptr) { Clear(); return; }
            m_status->SetText(u8"Debugger: paused");
            m_stackList->RemoveAllViews(true);
            for (const script::ScriptStackFrame& frame : m_debugger->CaptureStackFrames())
            {
                String text(frame.function.AsView());
                text += u8"  (";
                text += frame.file;
                text += u8":";
                AppendInt(text, frame.line);
                text += u8")";
                AddRow(*m_stackList, text.AsView(), 0.0f);
            }
            m_localsList->RemoveAllViews(true);
            for (const script::ScriptVariable& local : m_debugger->CaptureLocals(0))
            {
                AddLocalRow(local, 0.0f);
            }
        }

        void Clear()
        {
            m_status->SetText(u8"Debugger: running");
            m_stackList->RemoveAllViews(true);
            m_localsList->RemoveAllViews(true);
        }

        void SetIdle()
        {
            m_debugger = nullptr;
            m_expanded.Clear();
            m_status->SetText(u8"Debugger: not running");
            m_stackList->RemoveAllViews(true);
            m_localsList->RemoveAllViews(true);
        }

        /// A locals-expand click flagged a rebuild (consumed by the Game page's OnUpdate, so the
        /// view tree is never mutated mid-event-dispatch).
        [[nodiscard]] bool ConsumeDirty() noexcept
        {
            const bool was = m_dirty;
            m_dirty = false;
            return was;
        }

    private:
        [[nodiscard]] static RefPtr<ui::FlexLayoutParams> MatchWidth()
        {
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            return lp;
        }

        template <typename Fn>
        void AddToolButton(ui::FlexLayout& bar, StringView label, Fn onClick)
        {
            auto button = MakeRef<ui::Button>(DefaultAllocator(), label);
            button->FontSize.SetValue(12.0f);
            button->OnClick.Add([onClick](ui::ButtonBase*) { onClick(); });
            bar.AddView(button.Get(), RefPtr<ui::FlexLayoutParams>{});
        }

        void AddRow(ui::FlexLayout& list, StringView text, f32 indent)
        {
            auto label = MakeRef<ui::Label>(DefaultAllocator(), text);
            label->FontSize.SetValue(12.0f);
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Margin = ui::Thickness{ indent, 0, 0, 0 };
            list.AddView(label.Get(), lp);
        }

        void AddLocalRow(const script::ScriptVariable& variable, f32 indent)
        {
            String text(variable.name.AsView());
            text += u8" = ";
            text += variable.value;
            if (!variable.typeName.IsEmpty())
            {
                text += u8"  (";
                text += variable.typeName;
                text += u8")";
            }
            const bool expandable = variable.objectRef != 0;
            const bool expanded = expandable && IsExpanded(variable.objectRef);
            if (expandable)
            {
                // A row with an ASCII expand toggle (the editor font renders only <=255).
                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 4.0f;
                auto toggle = MakeRef<ui::Button>(DefaultAllocator(),
                    StringView(expanded ? u8"-" : u8"+"));
                toggle->FontSize.SetValue(12.0f);
                DebuggerPanel* self = this;
                const u64 ref = variable.objectRef;
                toggle->OnClick.Add([self, ref](ui::ButtonBase*) { self->ToggleExpand(ref); });
                row->AddView(toggle.Get(), RefPtr<ui::FlexLayoutParams>{});
                auto label = MakeRef<ui::Label>(DefaultAllocator(), text.AsView());
                label->FontSize.SetValue(12.0f);
                row->AddView(label.Get(), RefPtr<ui::FlexLayoutParams>{});
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Match();
                lp->Margin = ui::Thickness{ indent, 0, 0, 0 };
                m_localsList->AddView(row.Get(), lp);
                if (expanded && m_debugger != nullptr)
                {
                    for (const script::ScriptVariable& member : m_debugger->CaptureObject(ref))
                    {
                        String memberText(member.name.AsView());
                        memberText += u8" = ";
                        memberText += member.value;
                        AddRow(*m_localsList, memberText.AsView(), indent + 18.0f);
                    }
                }
            }
            else
            {
                AddRow(*m_localsList, text.AsView(), indent);
            }
        }

        [[nodiscard]] bool IsExpanded(u64 ref) const
        {
            for (u64 e : m_expanded) { if (e == ref) { return true; } }
            return false;
        }

        void ToggleExpand(u64 ref)
        {
            for (usize i = 0; i < m_expanded.Size(); ++i)
            {
                if (m_expanded[i] == ref) { m_expanded.RemoveAt(i); m_dirty = true; return; }
            }
            m_expanded.PushBack(ref);
            m_dirty = true;
        }

        static void AppendInt(String& out, i32 value)
        {
            if (value < 0) { out.PushBack(utf8char('-')); value = -value; }
            utf8char digits[16];
            i32 n = 0;
            u32 v = static_cast<u32>(value);
            do { digits[n++] = static_cast<utf8char>('0' + v % 10); v /= 10; } while (v > 0 && n < 16);
            while (n > 0) { out.PushBack(digits[--n]); }
        }

        RefPtr<ui::View> m_root;
        RefPtr<ui::Label> m_status;
        RefPtr<ui::FlexLayout> m_stackList;
        RefPtr<ui::FlexLayout> m_localsList;
        script::IScriptDebugger* m_debugger = nullptr;   // borrowed (owned by the run host)
        Array<u64> m_expanded;                            // expanded object refs (per break)
        bool m_dirty = false;
    };

    // The Game page's debugger sink: it never touches views (that would be mid-script-dispatch);
    // it only records the latest state + a dirty flag the page drains from OnUpdate.
    class GameDebugListener final : public script::IScriptDebuggerListener
    {
    public:
        script::ScriptDebuggerState state = script::ScriptDebuggerState::Running;
        bool changed = false;
        void OnDebuggerStateChanged(script::ScriptDebuggerState newState) override
        {
            state = newState;
            changed = true;
        }
    };

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
        ui::viewport::ViewportView* viewport = nullptr;          // borrowed
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
        [[nodiscard]] Span<const draconic::shell::InputEvent> Events() override
        {
            // Key/text events stream only while the viewport owns keyboard focus - the
            // same gate SurfaceKeyboard applies to the polled reads.
            auto* surface = viewport != nullptr ? viewport->Surface() : nullptr;
            if (surface == nullptr || !surface->Focused() || shellInput == nullptr) { return {}; }
            return shellInput->Events();
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
        GameEditorPage(EditorContext& context, runtime::IApplicationHost& host,
                       ui::runtime::UIHost& uiHost, runtime::DefaultApplication* embeddedApp,
                       runtime::GameInstance* instance)
            : m_context(&context), m_host(&host), m_uiHost(&uiHost), m_app(embeddedApp),
              m_gameInstance(instance)
        {
            m_scenes = host.Ctx().GetSubsystem<scene::SceneSubsystem>();
            m_render = host.Ctx().GetSubsystem<render::RenderSubsystem>();
            m_input = host.Ctx().GetSubsystem<draconic::input::InputSubsystem>();
            m_shellInput = host.Shell() != nullptr ? host.Shell()->Input() : nullptr;

            m_viewport = MakeRef<ui::viewport::ViewportView>(DefaultAllocator());
            m_viewport->ClearColor = rhi::ClearColor{ 0.05f, 0.05f, 0.06f, 1.0f };

            // "Exit" from embedded game code = stop this play session (deferred by the
            // app to after the page-update loop - never torn down mid-script-dispatch).
            context.StopGameRun = Function<void()>{ [this]() { Stop(); } };

            // Toolbar: Play / Stop / Restart + the run-state readout.
            GameEditorPage* self = this;
            m_toolbar = MakeRef<ui::toolkit::Toolbar>(DefaultAllocator());
            m_playButton = m_toolbar->AddButton(u8"Play");
            m_playButton->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->Play(); });
            m_pauseToggle = m_toolbar->AddToggle(u8"Pause");
            m_pauseToggle->OnCheckedChanged.Add([self](ui::toolkit::ToolbarToggle*, bool paused) {
                if (self->m_scene != nullptr && self->m_running)
                {
                    self->m_scene->SetSimulationEnabled(!paused);
                }
            });
            m_stopButton = m_toolbar->AddButton(u8"Stop");
            m_stopButton->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->Stop(); });
            m_restartButton = m_toolbar->AddButton(u8"Restart");
            m_restartButton->OnClick.Add([self](ui::toolkit::ToolbarButton*) {
                self->Stop();
                self->Play();
            });
            // Preview resolution: Auto (panel size) / Deck 1280x800 / 1080p - letterboxed,
            // with mouse AND touch input mapping through the same fit.
            m_resolutionButton = m_toolbar->AddButton(u8"Res: Auto");
            m_resolutionButton->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->CycleResolution(); });
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
            // The play stage: the game viewport (grows) beside the debugger panel (fixed).
            auto stage = MakeRef<ui::FlexLayout>(DefaultAllocator());
            stage->Direction = ui::Orientation::Horizontal;
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->Height = draconic::ui::SizeSpec::Match();
                stage->AddView(m_viewport.Get(), lp);
            }
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Fixed(draconic::ui::Unit::Px(300));
                lp->Height = draconic::ui::SizeSpec::Match();
                stage->AddView(m_debuggerPanel.RootView(), lp);
            }
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                lp->Grow = 1.0f;
                column->AddView(stage.Get(), lp);
            }
            m_content = column;
            RefreshToolbar();
        }

        [[nodiscard]] StringView Title() const override { return u8"Game"; }
        [[nodiscard]] Status Save() override { return Status{}; }   // nothing here is a document
        [[nodiscard]] draconic::ui::View* ContentView() override { return m_content.Get(); }

        // The scene group this tab's game scene belongs to: the embedded app's GameInstance manager
        // (game-instance.md §11), so the game scene groups + ticks + is bound to the instance's run
        // host. Falls back to THIS page's own (unregistered) manager if there's no embedded app - a
        // defensive placeholder, since without a runtime there is no game to run anyway.
        [[nodiscard]] scene::SceneManager& SceneGroup() noexcept
        {
            return (m_gameInstance != nullptr) ? m_gameInstance->Scenes() : m_fallbackScenes;
        }

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
            // Via THIS tab's instance (not just SceneGroup) so behaviors bind to its run host.
            m_scene = (m_gameInstance != nullptr) ? m_gameInstance->CreateScene(instance->Name())
                                                  : m_fallbackScenes.CreateScene(instance->Name());
            if (m_scene == nullptr || !scene::LoadScene(*instance, *m_scene).IsOk())
            {
                m_context->Notify(NoticeKind::Error, u8"Game: default scene failed to load.");
                if (m_scene != nullptr) { SceneGroup().DestroyScene(m_scene); m_scene = nullptr; }
                return;
            }
            // Products bind from the cooked DB (the editor's shared manager); prefab payloads
            // come from the source DB - the same split RaptorPlayer uses in project mode.
            if (m_context->Resources() != nullptr)
            {
                scene::ResolveSceneResources(*m_scene, *m_context->Resources());
            }
            if (m_scene->PendingPrefabInstanceCount() > 0)
            {
                EditorContext* context = m_context;
                scene::ResolveScenePrefabs(*m_scene,
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
                    scene::ResolveSceneResources(*m_scene, *m_context->Resources());
                }
            }
            EnsureCamera();

            m_scene->Start();
            m_scene->SetSimulationEnabled(true);
            m_running = true;
            if (m_pauseToggle != nullptr) { m_pauseToggle->SetIsChecked(false); }
            m_sceneTitle = String(instance->Name());
            BindInput();
            BindBusLayout(*m_host);
            // The play bracket + game script run on THIS tab's GameInstance (game-instance.md §11):
            // its own scene pairing, run host, error sink - so multiple tabs are isolated. The page
            // only resolves the script SOURCE (editor project layout) and surfaces notices.
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->SetScene(m_scene);
                m_scriptErrors.context = m_context;
                m_gameInstance->SetScriptErrorHandler(&m_scriptErrors);
                EnableDebugging();
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
            // the runtime input's permanent provider (v3) - but its SCENE BINDING drops
            // with the run, returning the editor to inert (ScreenTierOnly) game UI.
            if (m_input != nullptr)
            {
                m_input->SetMap(draconic::input::InputMap{});
                m_input->SetSourceProvider(&m_viewportSource, nullptr);
            }
            // Drop the debugger wiring BEFORE the run tears the debugger down (the panel
            // holds a borrowed pointer; the run host owns + destroys it in OnExit).
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->RunHost().SetExternalDebugListener(nullptr);
            }
            m_debuggerPanel.SetIdle();
            m_simPausedByDebugger = false;
            // Script exits first (it may still observe the world), then the scene.
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->StopScript();
                m_gameInstance->SetScriptErrorHandler(nullptr);
                m_gameInstance->SetScene(nullptr);
            }
            if (m_scene != nullptr)
            {
                m_scene->Stop();
                if (m_scenes != nullptr) { SceneGroup().DestroyScene(m_scene); }
                m_scene = nullptr;
            }
            m_running = false;
            RefreshToolbar();
        }

        // Bind (and re-bind after dock/float moves) the viewport to the window hosting it -
        // the same RendererFor dance as ScenePage. Without this the viewport never gets a
        // DEVICE: no color target, IsReady() false, and the tab renders only its clear color.
        void EnsureViewportBound()
        {
            draconic::ui::RootView* root = m_viewport->Root();
            if (root == nullptr) { return; }
            draconic::graphics::RenderWindow* window = m_uiHost->WindowForRoot(root);
            if (window == nullptr || window == m_hostWindow) { return; }

            vg::renderer::VGRenderer* renderer = m_uiHost->RendererFor(window);
            if (renderer == nullptr) { return; }   // float's AttachWindow hasn't run yet

            if (m_hostWindow == nullptr)
            {
                m_viewport->Initialize(m_host->Graphics()->Raw(), renderer,
                                       m_host->Shell()->Input(), window->Window().Id());
                // The (now-existing) gated surface is the runtime input's permanent
                // source. The SCENE BINDING rides the play state: bound to the fresh
                // run's scene while playing (BindInput), un-bound otherwise - which,
                // under the editor's ScreenTierOnly policy, keeps game UI inert until
                // Play (editing-page HUDs render but never take editor input).
                m_viewportSource.viewport = m_viewport.Get();
                m_viewportSource.shellInput = m_shellInput;
                // Gate the viewport surface (hover/focus) via an InputRouter, like ScenePage. Without
                // it the surface is never focused, so SurfaceKeyboard reports every key UP and the game
                // reads no keyboard - the game viewport must own a router or its input is dead.
                if (m_router.Get() == nullptr)
                {
                    m_router = MakeUnique<draconic::shell::InputRouter>(
                        DefaultAllocator(), m_host->Shell()->Input());
                }
                if (m_viewport->Surface() != nullptr) { m_router->AddSurface(m_viewport->Surface()); }
                if (m_input != nullptr)
                {
                    m_input->SetSourceProvider(&m_viewportSource,
                                               m_running ? static_cast<const void*>(m_scene) : nullptr);
                }
            }
            else
            {
                m_viewport->AttachToWindow(renderer, window->Window().Id());
            }
            m_hostWindow = window;
        }

        void OnUpdate(runtime::IApplicationHost& host, f32 dt) override
        {
            EnsureViewportBound();
            m_viewport->SyncInputRegion();
            if (m_router.Get() != nullptr) { m_router->Update(); }   // gate the surface: hover=mouse, click=keyboard focus
            // IME follows the GAME UI's focus through the host window: the viewport (the
            // editor context's focused view while playing) forwards the game context's
            // WantsTextInput, and the editor's own input bridge does the Start/Stop.
            m_viewport->SetHostedTextInputWanted(
                m_app != nullptr && m_app->UI() != nullptr &&
                m_app->UI()->Context().WantsTextInput());
            // The embedded app's OnUpdate (ticking EVERY instance's game script) is driven ONCE by
            // the editor app now (game-instance.md §11 step 5) - not per game tab, or N tabs would
            // tick every instance N times. This tab only drains its own debugger state.
            (void)host; (void)dt;
            DrainDebuggerState();
        }

        void OnRenderWindow(runtime::IApplicationHost&, draconic::graphics::FrameContext& frame) override
        {
            if (!m_viewport->IsReady() || !frame.valid) { return; }
            // Idle (no run): the editor UI still SAMPLES the viewport texture every frame,
            // so it must be in a defined shader-read layout - clear it once per frame.
            // (Every other viewport page renders every frame; only the Game tab idles.)
            if (m_scene == nullptr)
            {
                m_viewport->ClearContent(*frame.encoder);
                return;
            }
            if (m_render == nullptr || !m_render->IsReady()) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0) { return; }
            if (!m_viewport->IsEffectivelyVisible()) { return; }

            // RenderTexture canvases draw before the scene (the same host seam the
            // player runs in DefaultApplication::OnRenderWindow).
            if (m_app != nullptr && m_app->UI() != nullptr)
            {
                m_app->UI()->RenderCanvasTextures(*frame.encoder, static_cast<i32>(frame.frameIndex));
            }

            // No camera override: the SCENE's primary camera drives the view (its clear
            // color included) - the player's presentation, not the editor's.
            render::TargetState targetState;
            targetState.texture = m_viewport->ColorTexture();
            targetState.currentState = m_viewport->ColorState();
            targetState.finalState = rhi::ResourceState::ShaderRead;
            m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(),
                                  w, h, render::ViewportRect{ 0, 0, w, h }, nullptr, targetState);
            m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
        }

        void OnAfterSceneRender(runtime::IApplicationHost& host,
                                draconic::graphics::FrameContext& frame) override
        {
            // Scene-tier UI (HUD canvases/billboards) already landed in the viewport
            // inside the compose. This composites the game's WINDOW-SPACE overlays
            // (screen-tier UI, diagnostics) onto the viewport through the generic
            // registry - the tab shows the same full output as the player's window.
            if (!m_running || m_scene == nullptr || !m_viewport->IsReady() || !frame.valid)
            {
                return;
            }
            auto* render = host.Ctx().GetSubsystem<render::RenderSubsystem>();
            if (render == nullptr) { return; }
            const u32 w = m_viewport->RenderWidth();
            const u32 h = m_viewport->RenderHeight();
            if (w == 0 || h == 0) { return; }
            frame.encoder->TransitionTexture(m_viewport->ColorTexture(),
                                             m_viewport->ColorState(),
                                             rhi::ResourceState::RenderTarget);
            render->RenderOverlays(*frame.encoder, m_viewport->ColorTargetView(),
                                   m_viewport->ColorFormat(), w, h, frame.frameIndex);
            frame.encoder->TransitionTexture(m_viewport->ColorTexture(),
                                             rhi::ResourceState::RenderTarget,
                                             rhi::ResourceState::ShaderRead);
            m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
        }

        void OnClose() override
        {
            Stop();
            // Stop leaves this page's viewport source as the input override (by design, so a STOPPED-
            // but-open tab keeps viewport input). On CLOSE the source is about to be freed, so clear it
            // or ActiveSource() dangles and the next PumpInput crashes (guarded: only if it's ours).
            if (m_input != nullptr) { m_input->ClearSourceProviderIf(&m_viewportSource); }
            // The PER-INSTANCE input source (BindInput set it to this tab's viewport) also dangles once
            // m_viewportSource is freed - and ReleaseInstance below is a NO-OP for the PRIMARY instance,
            // so its DriveInput would dereference the freed source next frame. Revert it to the shell.
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->SetInputSource(m_input != nullptr ? &m_input->ShellSource() : nullptr);
            }
            // Destroy THIS tab's extra instance (unregisters its scene manager + tears down its run
            // host) so nothing dangling is ticked/rendered after the tab closes. No-op for the primary.
            if (m_app != nullptr && m_gameInstance != nullptr) { m_app->ReleaseInstance(m_gameInstance); }
            m_gameInstance = nullptr;
            m_context->StopGameRun = Function<void()>{};
            m_viewport->Shutdown();
        }

    private:
        // Authored game scenes should carry a camera; a bare scene shouldn't play as a black
        // screen - frame the origin like RaptorPlayer does.
        void EnsureCamera()
        {
            auto* cameras = m_scene->GetSystem<render::CameraComponentManager>();
            if (cameras == nullptr) { return; }
            bool hasCamera = false;
            cameras->ForEach([&](render::CameraComponent&, scene::EntityHandle) { hasCamera = true; });
            if (hasCamera) { return; }

            DRACONIC_LOG_WARNING(u8"Editor", u8"Game: scene has no camera - adding a default one");
            const scene::EntityHandle e = m_scene->CreateEntity(u8"PlayerCamera");
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
            // Per-surface scene binding (game-ui.md §9): the viewport source represents
            // THIS run's scene, so game-UI routing + consumption confine to it - open
            // editing pages' HUDs can no longer catch the run's clicks/keys, and the
            // run's UI never reacts to another scene's coordinates.
            m_input->SetSourceProvider(&m_viewportSource, m_scene);   // UI-pump active source (game-UI routing)
            // Per-instance INPUT: this tab's game reads its OWN viewport source through its OWN action
            // runtime, so two Game tabs never cross-feed keys (only the FOCUSED tab's surface reports
            // them). The shared subsystem runtime is no longer the game's input.
            if (m_gameInstance != nullptr) { m_gameInstance->SetInputSource(&m_viewportSource); }
            const Guid mapId = m_context->Project()->Settings().defaultInputMapId;
            if (mapId.IsNil() || m_context->Resources() == nullptr) { return; }
            auto proxy = m_context->Resources()->Bind<draconic::input::InputMapResource>(mapId);
            if (proxy)
            {
                if (m_gameInstance != nullptr) { m_gameInstance->SetInputMap(proxy->Map()); }
                DRACONIC_LOG_INFO(u8"Editor", u8"Game: input map bound ({} set(s))",
                                  proxy->Map().sets.Size());
            }
            else
            {
                m_context->Notify(NoticeKind::Warning,
                    u8"Game: default input map is not cooked yet.");
            }
        }

        // The project's default audio bus layout into the embedded engine (the player's
        // startup twin) - nil/unresolved = the built-in neutral layout stays.
        void BindBusLayout(runtime::IApplicationHost& host)
        {
            auto* audio = host.Ctx().GetSubsystem<draconic::audio::AudioSubsystem>();
            if (audio == nullptr || audio->Engine() == nullptr) { return; }
            const Guid layoutId = m_context->Project()->Settings().defaultBusLayoutId;
            if (layoutId.IsNil() || m_context->Resources() == nullptr) { return; }
            auto proxy =
                m_context->Resources()->Bind<draconic::audio::AudioBusLayoutResource>(layoutId);
            if (proxy)
            {
                audio->Engine()->ApplyBusLayout(proxy->layout);
                DRACONIC_LOG_INFO(u8"Editor", u8"Game: audio bus layout applied");
            }
            else
            {
                m_context->Notify(NoticeKind::Warning,
                    u8"Game: default bus layout is not cooked yet.");
            }
        }

        // Resolves the startup script's SOURCE (editor project layout); the lifecycle -
        // facades, services, launch/update/exit, fault handling - is the embedded app's.
        void StartGameScriptFromProject()
        {
            // The startup script is a cooked ScriptClass asset (guid-authoritative), bound from the
            // content DB like every other asset - no raw source path.
            const Guid scriptId = m_context->Project()->Settings().startupScriptId;
            if (scriptId.IsNil() || m_context->Resources() == nullptr) { return; }
            auto proxy = m_context->Resources()->Bind<draconic::script::ScriptClass>(scriptId);
            if (!proxy || proxy->source.IsEmpty())
            {
                m_context->Notify(NoticeKind::Warning, u8"Game: startup script asset not found.");
                return;
            }
            if (m_gameInstance == nullptr
                || !m_gameInstance->StartScript(proxy->source.AsView(), proxy->sourceName.AsView()))
            {
                m_context->Notify(NoticeKind::Error,
                    u8"Game: startup script failed to start (see Console).");
            }
        }

        // Make this run debuggable: request a debugger on the run's script manager and, once it
        // exists, apply the editor's breakpoints + hand it to the panel. Lazy - the debugger is
        // created when the script context is (first behavior / game script).
        void EnableDebugging()
        {
            if (m_gameInstance == nullptr) { return; }
            m_gameInstance->RunHost().SetExternalDebugListener(&m_debugListener);
            EditorContext* context = m_context;
            GameEditorPage* self = this;
            m_gameInstance->RunHost().RequestDebugger(
                Function<void(script::IScriptDebugger&)>{
                    [context, self](script::IScriptDebugger& debugger) {
                        for (const EditorContext::ScriptBreakpoint& breakpoint : context->Breakpoints())
                        {
                            debugger.SetBreakpoint(breakpoint.file.AsView(), breakpoint.line);
                        }
                        self->m_debuggerPanel.SetDebugger(&debugger);
                    } });
        }

        // Drain the debugger's state changes at the top level (never mid-script-dispatch): on a
        // break, freeze the whole scene simulation (physics + behaviors) and populate the panel;
        // on resume, thaw and clear it. A locals-expand click also rebuilds here.
        void DrainDebuggerState()
        {
            if (!m_running) { return; }
            if (m_debugListener.changed)
            {
                m_debugListener.changed = false;
                const bool paused =
                    m_debugListener.state == script::ScriptDebuggerState::Breakpoint
                    || m_debugListener.state == script::ScriptDebuggerState::Stepped;
                if (paused)
                {
                    if (m_scene != nullptr && !m_simPausedByDebugger)
                    {
                        m_scene->SetSimulationEnabled(false);
                        m_simPausedByDebugger = true;
                    }
                    m_debuggerPanel.Refresh();
                }
                else
                {
                    if (m_scene != nullptr && m_simPausedByDebugger)
                    {
                        m_scene->SetSimulationEnabled(true);
                        m_simPausedByDebugger = false;
                    }
                    m_debuggerPanel.Clear();
                }
            }
            if (m_debuggerPanel.ConsumeDirty()) { m_debuggerPanel.Refresh(); }
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
        runtime::IApplicationHost* m_host = nullptr;
        ui::runtime::UIHost* m_uiHost = nullptr;
        runtime::DefaultApplication* m_app = nullptr;   // the embedded game application (v3)
        runtime::GameInstance* m_gameInstance = nullptr;   // THIS tab's running game (its own run host + scenes)
        draconic::graphics::RenderWindow* m_hostWindow = nullptr;   // borrowed; tracks dock/float moves
        scene::SceneSubsystem* m_scenes = nullptr;
        render::RenderSubsystem* m_render = nullptr;
        scene::Scene* m_scene = nullptr;
        draconic::input::InputSubsystem* m_input = nullptr;
        draconic::shell::IInputManager* m_shellInput = nullptr;
        GameViewportInputSource m_viewportSource;

        RefPtr<draconic::ui::View> m_content;
        RefPtr<ui::toolkit::Toolbar> m_toolbar;
        ui::toolkit::ToolbarButton* m_playButton = nullptr;
        ui::toolkit::ToolbarButton* m_stopButton = nullptr;
        ui::toolkit::ToolbarToggle* m_pauseToggle = nullptr;
        ui::toolkit::ToolbarButton* m_restartButton = nullptr;
        ui::toolkit::ToolbarButton* m_resolutionButton = nullptr;
        u32 m_resolutionMode = 0;
        GameScriptErrorSink m_scriptErrors;
        DebuggerPanel m_debuggerPanel;              // the debugger UI (contract-only)
        GameDebugListener m_debugListener;          // debugger state sink (drained in OnUpdate)
        bool m_simPausedByDebugger = false;         // we disabled sim for a breakpoint
        RefPtr<draconic::ui::Label> m_statusLabel;
        RefPtr<ui::viewport::ViewportView> m_viewport;
        UniquePtr<draconic::shell::InputRouter> m_router;   // gates the viewport surface (hover/focus)
        scene::SceneManager m_fallbackScenes;   // no-embedded-app placeholder group (see SceneGroup)


        String m_sceneTitle;
        bool m_running = false;
    };
}
