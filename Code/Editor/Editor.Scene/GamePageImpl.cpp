// Editor::Scene - :game_page partition.
//
// GameEditorPage: a singleton
// "Game" dock tab hosting the PLAYER behavior - a FRESH run of the project's default scene,
// exactly what Engine.Player does, in-process. Distinct from the ScenePage's Simulate
// (in-place snapshot -> run -> restore): nothing here is edited, so Play builds everything
// from scratch (fresh Scene + resolve + Start) and Stop tears it all down - total cleanup IS
// the restore. Renders through the scene's own primary camera (RenderScene with no override;
// EnsureCamera frames the origin when the scene ships none, like the player).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module editor.scene;

import foundation.core;
import foundation.content;
import foundation.vfs;
import foundation.resource;
import foundation.scene;
import foundation.scene.resource;
import engine.render;
import foundation.runtime;
import foundation.runtime.client;
import foundation.graphics;
import engine.scene;
import foundation.rhi;
import foundation.ui;
import foundation.ui.toolkit;
import foundation.ui.runtime;
import foundation.vg.renderer;
import foundation.ui.viewport;
import foundation.script;
import foundation.script.resource;  // ScriptClass (the cooked game script, bound from the content DB)
import engine.script; // ScriptSubsystem / ScriptRunHost (debugger wiring)
import foundation.shell;
import foundation.input;
import foundation.input.resource;
import engine.input;
import engine.physics;
import foundation.audio;
import foundation.audio.resource;
import engine.audio;
import engine.defaultapp;
import engine.gameinstance; // GameInstance - this tab drives its OWN run (multi-instance PIE)
import editor.core;
import editor.app;

using namespace foundation::core;
namespace render = foundation::render;
namespace rhi = foundation::rhi;
namespace runtime = foundation::runtime;
namespace scene = foundation::scene;
namespace script = foundation::script;
namespace ui = foundation::ui;
namespace vg = foundation::vg;

namespace editor
{
    void DebuggerPanel::SetDebugger(script::IScriptDebugger* debugger)
    {
        m_debugger = debugger;
        m_expanded.Clear();
        if (debugger == nullptr)
        {
            Clear();
        }
    }

    void DebuggerPanel::Refresh()
    {
        if (m_debugger == nullptr)
        {
            Clear();
            return;
        }
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

    void DebuggerPanel::Clear()
    {
        m_status->SetText(u8"Debugger: running");
        m_stackList->RemoveAllViews(true);
        m_localsList->RemoveAllViews(true);
    }

    void DebuggerPanel::SetIdle()
    {
        m_debugger = nullptr;
        m_expanded.Clear();
        m_status->SetText(u8"Debugger: not running");
        m_stackList->RemoveAllViews(true);
        m_localsList->RemoveAllViews(true);
    }

    bool DebuggerPanel::ConsumeDirty() noexcept
    {
        const bool was = m_dirty;
        m_dirty = false;
        return was;
    }

    RefPtr<ui::FlexLayoutParams> DebuggerPanel::MatchWidth()
    {
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Match();
        return lp;
    }

    void DebuggerPanel::AddRow(ui::FlexLayout& list, StringView text, f32 indent)
    {
        auto label = MakeRef<ui::Label>(DefaultAllocator(), text);
        label->FontSize.SetValue(12.0f);
        auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
        lp->Width = ui::SizeSpec::Match();
        lp->Margin = ui::Thickness{indent, 0, 0, 0};
        list.AddView(label.Get(), lp);
    }

    void DebuggerPanel::AddLocalRow(const script::ScriptVariable& variable, f32 indent)
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
            auto toggle =
                MakeRef<ui::Button>(DefaultAllocator(), StringView(expanded ? u8"-" : u8"+"));
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
            lp->Margin = ui::Thickness{indent, 0, 0, 0};
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

    bool DebuggerPanel::IsExpanded(u64 ref) const
    {
        for (u64 e : m_expanded)
        {
            if (e == ref)
            {
                return true;
            }
        }
        return false;
    }

    void DebuggerPanel::ToggleExpand(u64 ref)
    {
        for (usize i = 0; i < m_expanded.Size(); ++i)
        {
            if (m_expanded[i] == ref)
            {
                m_expanded.RemoveAt(i);
                m_dirty = true;
                return;
            }
        }
        m_expanded.PushBack(ref);
        m_dirty = true;
    }

    void DebuggerPanel::AppendInt(String& out, i32 value)
    {
        if (value < 0)
        {
            out.PushBack(utf8char('-'));
            value = -value;
        }
        utf8char digits[16];
        i32 n = 0;
        u32 v = static_cast<u32>(value);
        do
        {
            digits[n++] = static_cast<utf8char>('0' + v % 10);
            v /= 10;
        } while (v > 0 && n < 16);
        while (n > 0)
        {
            out.PushBack(digits[--n]);
        }
    }
    void GameDebugListener::OnDebuggerStateChanged(script::ScriptDebuggerState newState)
    {
        state = newState;
        changed = true;
    }
    scene::SceneManager& GameEditorPage::SceneGroup() noexcept
    {
        return (m_gameInstance != nullptr) ? m_gameInstance->Scenes() : m_fallbackScenes;
    }

    void GameEditorPage::Play()
    {
        if (m_running || m_pendingPlay)
        {
            return;
        }
        // PIE waits for the cook: kick an incremental cook NOW and
        // defer the actual start to OnUpdate once the service is idle - a run that starts
        // mid-cook binds stale or missing products (fonts, scripts, input maps) and the
        // "heal via hot reload" path never covered a script that did not exist yet.
        // Unwired CookBusy (tests, no cook service) starts on the next OnUpdate tick.
        if (m_context->OnCookRequested)
        {
            m_context->OnCookRequested(false);
        }
        m_pendingPlay = true;
        if (m_context->IsCookBusy())
        {
            m_context->SetStatus(u8"Game: waiting for cook...");
        }
    }

    void GameEditorPage::StartRunNow()
    {
        if (m_running)
        {
            return;
        }
        if (m_scenes == nullptr || m_context->Project() == nullptr)
        {
            return;
        }

        // Resolution order mirrors Engine.Player: the manifest's guid (authoritative,
        // rename-proof), then the path mirror.
        editor::EditorProject& project = *m_context->Project();
        foundation::content::Instance* instance = nullptr;
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
            // No default scene: a scripted game owns boot (the player's "the game script owns boot"
            // path). The run still starts - it just renders nothing until the game loads its first
            // scene via run.loadScene, which the follow-poll in OnUpdate then adopts. If the project
            // has neither a default scene nor a startup script, the tab simply shows nothing.
            LOG_INFO(u8"Editor", u8"Game: no default scene - the game script owns boot.");
        }

        // The cook was requested at Play() and has finished by the time OnUpdate routes here
        // (the m_pendingPlay gate) - products bound below are fresh, not healed-later.
        // The play bracket + game script run on THIS tab's GameInstance: its
        // own scene pairing, run host, error sink - so multiple tabs are isolated. Launch the game
        // script FIRST (matching Engine.Player): launch()/update(dt)
        // run before any scene, so a script tested here boots exactly like the player. The
        // page only resolves the script SOURCE (editor project layout) and surfaces notices.
        if (m_gameInstance != nullptr)
        {
            m_scriptErrors.context = m_context;
            m_gameInstance->SetScriptErrorHandler(&m_scriptErrors);
            EnableDebugging();
            StartGameScriptFromProject();
        }

        // A resolved default scene loads + starts here, exactly like the player's default-scene boot
        // (the script launched first, above). A project with none skips this block: the run starts
        // with no scene, and the follow-poll in OnUpdate adopts whatever the game script loads.
        if (instance != nullptr)
        {
            // Via THIS tab's instance (not just SceneGroup) so behaviors bind to its run host.
            scene::Scene* startScene = (m_gameInstance != nullptr)
                                           ? m_gameInstance->CreateScene(instance->Name())
                                           : m_fallbackScenes.CreateScene(instance->Name());
            if (startScene == nullptr || !scene::LoadScene(*instance, *startScene).IsOk())
            {
                m_context->Notify(NoticeKind::Error, u8"Game: default scene failed to load.");
                if (startScene != nullptr)
                {
                    SceneGroup().DestroyScene(startScene);
                }
                if (m_gameInstance != nullptr)
                {
                    m_gameInstance
                        ->StopScript(); // the script launched first (above) - do not leave it running
                }
                return;
            }
            // Products bind from the cooked DB (the editor's shared manager); prefab payloads
            // come from the source DB - the same split Engine.Player uses in project mode.
            if (m_context->Resources() != nullptr)
            {
                scene::ResolveSceneResources(*startScene, *m_context->Resources());
            }
            if (startScene->PendingPrefabInstanceCount() > 0)
            {
                EditorContext* context = m_context;
                scene::ResolveScenePrefabs(
                    *startScene,
                    Function<UniquePtr<IStream>(const Guid&)>{
                        [context](const Guid& prefabId) -> UniquePtr<IStream>
                        {
                            if (context->Project() == nullptr)
                            {
                                return UniquePtr<IStream>{};
                            }
                            foundation::content::Instance* prefab =
                                context->Project()->SourceDb().GetInstance(prefabId);
                            return (prefab != nullptr) ? prefab->ReadData(u8"scene")
                                                       : UniquePtr<IStream>{};
                        }});
                if (m_context->Resources() != nullptr)
                {
                    scene::ResolveSceneResources(*startScene, *m_context->Resources());
                }
            }
            EnsureCamera(*startScene);
            startScene->Start();
            startScene->SetSimulationEnabled(true);
            // The script launched above (before the scene); now make the freshly built scene this
            // instance's current scene (behaviors already bound to its run host via CreateScene).
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->SetScene(startScene);
            }
            else
            {
                m_scene = startScene; // no instance to track a current scene (fallback path)
            }
        }

        m_running = true;
        if (m_pauseToggle != nullptr)
        {
            m_pauseToggle->SetIsChecked(false);
        }
        // Adopt the instance's current scene as this tab's borrowed render/input pointer: the default
        // we just started, a scene the game script loaded during launch(), or null (a scripted boot
        // that loads its first scene later). The follow-poll in OnUpdate tracks later run.loadScene
        // switches. Null is fine - the render + overlay paths early-return on a null scene.
        if (m_gameInstance != nullptr)
        {
            m_scene = m_gameInstance->GetScene();
        }
        m_sceneTitle = (m_scene != nullptr) ? String(m_scene->Name()) : String(u8"(no scene)");
        BindInput();
        BindBusLayout(*m_host);
        LOG_INFO(u8"Editor", u8"Game: running scene '{}'", m_sceneTitle);
        RefreshToolbar();
    }

    void GameEditorPage::Stop()
    {
        m_pendingPlay = false; // Stop while waiting for the cook cancels the deferred start
        if (!m_running && m_scene == nullptr)
        {
            return;
        }
        // The map clears (no actions bound between runs); the SOURCE stays - it is
        // the runtime input's permanent provider (v3) - but its SCENE BINDING drops
        // with the run, returning the editor to inert (ScreenTierOnly) game UI.
        if (m_input != nullptr)
        {
            m_input->SetMap(foundation::input::InputMap{});
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
        m_context->ClearScriptExecutionPoint(); // no run = no paused location
        m_context->ScriptValueProbe = {};       // hover-values die with the run
        m_context->OnBreakpointsChanged = {};   // live sync dies with the run
        m_appliedBreakpoints.Clear();
        // Script exits first (it may still observe the world), then the scenes.
        if (m_gameInstance != nullptr)
        {
            m_gameInstance->StopScript();
            m_gameInstance->SetScriptErrorHandler(nullptr);
        }
        // The instance's SceneManager owns EVERY scene this run created - the default scene AND any the
        // game script loaded via run.loadScene (LoadScene/SetScene never destroy a replaced scene, so
        // they accumulate on the group; the follow-poll retires outgoing ones during play, but this
        // final sweep catches whatever is current at stop plus any straggler). Stop each active scene's
        // behaviors while the aware subsystems are alive, then tear the whole group down. ClearScenes
        // also drops any in-flight tracked async load first, so a post-stop PumpScriptLoads can never
        // activate a just-freed scene. m_scene is a borrowed pointer into this group - just drop it.
        {
            Array<scene::Scene*> active;
            for (scene::Scene* s : SceneGroup().ActiveScenes())
            {
                active.PushBack(s);
            }
            for (scene::Scene* s : active)
            {
                s->Stop();
            }
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->ClearScenes();
            }
            else
            {
                m_fallbackScenes.Clear(); // no instance: the page's own placeholder group
            }
            m_scene = nullptr;
        }
        m_running = false;
        RefreshToolbar();
    }

    void GameEditorPage::FollowInstanceScene()
    {
        if (!m_running || m_gameInstance == nullptr)
        {
            return;
        }
        scene::Scene* current = m_gameInstance->GetScene();
        if (current == m_scene)
        {
            return;
        }
        // A scripted scene switch (run.loadScene, or an async load that just activated) repointed the
        // instance's current scene. The instance's SceneManager owns EVERY scene (default + loaded)
        // and never destroys a replaced one, so the tab retires the scenes it is leaving here. Adopt
        // the new current scene as our borrowed render/input pointer FIRST, then stop + destroy every
        // OTHER active scene in the group - exactly once, the manager being the sole owner (no double-
        // free: the instance already repointed its current pointer + net replication off the old one).
        m_scene = current; // may be null (a script that unloaded without loading; paths are null-safe)

        // Collect first: DestroyScene mutates the active set, so we cannot destroy mid-iteration. An
        // in-flight async-load scene is INACTIVE (not in ActiveScenes), so this never destroys one.
        Array<scene::Scene*> outgoing;
        for (scene::Scene* s : SceneGroup().ActiveScenes())
        {
            if (s != m_scene)
            {
                outgoing.PushBack(s);
            }
        }
        for (scene::Scene* s : outgoing)
        {
            s->Stop();
            m_gameInstance->DestroyScene(s); // drops any matching tracked load too, then frees the scene
        }

        // The adopted scene arrives already Started + simulation-enabled (run.loadScene's activation
        // policy) - do NOT re-Start it. Re-point the per-run input scene binding at it, honor an active
        // pause, and reflect the switch in the readout + log so they name the actually-rendered scene.
        if (m_input != nullptr)
        {
            m_input->SetSourceProvider(&m_viewportSource, m_scene);
        }
        if (m_scene != nullptr)
        {
            // A scene loaded via run.loadScene may ship no camera; give it a default one, like Play
            // does for the first scene - otherwise RenderScene draws nothing and the old frame lingers.
            EnsureCamera(*m_scene);
            const bool paused = m_simPausedByDebugger ||
                                (m_pauseToggle != nullptr && m_pauseToggle->IsChecked());
            if (paused)
            {
                m_scene->SetSimulationEnabled(false);
            }
            m_sceneTitle = String(m_scene->Name());
        }
        else
        {
            m_sceneTitle = String(u8"(no scene)");
        }
        LOG_INFO(u8"Editor", u8"Game: running scene '{}'", m_sceneTitle);
        RefreshToolbar();
    }

    void GameEditorPage::EnsureViewportBound()
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
                m_router = MakeUnique<foundation::shell::InputRouter>(DefaultAllocator(),
                                                                    m_host->Shell()->Input());
            }
            if (m_viewport->Surface() != nullptr)
            {
                m_router->AddSurface(m_viewport->Surface());
            }
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

    void GameEditorPage::OnUpdate(runtime::IApplicationHost& host, f32 dt)
    {
        // A Play pressed while the cook ran (or just kicked one) starts here, on the first
        // frame the cook service reports idle - never against a half-written cooked DB.
        if (m_pendingPlay && !m_context->IsCookBusy())
        {
            m_pendingPlay = false;
            StartRunNow();
        }
        // Follow the instance's current scene BEFORE anything renders this frame: a scripted switch
        // (run.loadScene) repointed it, so adopt the new scene + retire the outgoing one here.
        FollowInstanceScene();
        EnsureViewportBound();
        m_viewport->SyncInputRegion();
        if (m_router.Get() != nullptr)
        {
            // One app keyboard (issues repro: Tab in an editor dialog field traversed the PIE
            // game's menu): while the editor UI's keyboard focus is on any view but this
            // viewport, the router drops surface focus, closing the gated source the game's
            // devices, bindings, AND screen UI all read. Clicking the viewport re-focuses it
            // in the editor UI, reopening the gate.
            m_router->SetExternalCapture(false, m_viewport->HostKeyboardFocusElsewhere());
            m_router->Update();
        } // gate the surface: hover=mouse, click=keyboard focus
        // IME follows the GAME UI's focus through the host window: the viewport (the
        // editor context's focused view while playing) forwards the game context's
        // WantsTextInput, and the editor's own input bridge does the Start/Stop.
        m_viewport->SetHostedTextInputWanted(m_app != nullptr && m_app->UI() != nullptr &&
                                             m_app->UI()->Context().WantsTextInput());
        // The embedded app's OnUpdate (ticking EVERY instance's game script) is driven ONCE by
        // the editor app - not per game tab, or N tabs would
        // tick every instance N times. This tab only drains its own debugger state.
        (void)host;
        (void)dt;
        DrainDebuggerState();
    }

    void GameEditorPage::OnRenderWindow(runtime::IApplicationHost&,
                                        foundation::graphics::FrameContext& frame)
    {
        if (!m_viewport->IsReady() || !frame.valid)
        {
            return;
        }
        // Idle (no run): the editor UI still SAMPLES the viewport texture every frame,
        // so it must be in a defined shader-read layout - clear it once per frame.
        // (Every other viewport page renders every frame; only the Game tab idles.)
        if (m_scene == nullptr)
        {
            m_viewport->ClearContent(*frame.encoder);
            return;
        }
        if (m_render == nullptr || !m_render->IsReady())
        {
            return;
        }
        const u32 w = m_viewport->RenderWidth();
        const u32 h = m_viewport->RenderHeight();
        if (w == 0 || h == 0)
        {
            return;
        }
        if (!m_viewport->IsEffectivelyVisible())
        {
            return;
        }

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
        m_render->RenderScene(*m_scene, m_viewport->ColorTargetView(), m_viewport->ColorFormat(), w,
                              h, render::ViewportRect{0, 0, w, h}, nullptr, targetState);
        m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
    }

    void GameEditorPage::OnAfterSceneRender(runtime::IApplicationHost& host,
                                            foundation::graphics::FrameContext& frame)
    {
        // Scene-tier UI (HUD canvases/billboards) is already drawn in the viewport
        // inside the compose. This composites the game's WINDOW-SPACE overlays
        // (screen-tier UI, diagnostics) onto the viewport through the generic
        // registry - the tab shows the same full output as the player's window.
        if (!m_running || m_scene == nullptr || !m_viewport->IsReady() || !frame.valid)
        {
            return;
        }
        // The screen-tier overlay is registered against the EMBEDDED RUNTIME context's
        // RenderSubsystem (the one the embedded DefaultApplication configured). The
        // `host` handed to this callback is the editor's OWN outer host, whose context
        // carries no gameplay subsystems - resolving the RenderSubsystem through it
        // returns null and the game's screen-tier UI never composites. Use m_render:
        // the runtime RenderSubsystem the ctor cached from the embedded host, the same
        // one the game UISubsystem self-registered its IScreenOverlay with.
        (void)host;
        engine::render::RenderSubsystem* render = m_render;
        if (render == nullptr)
        {
            return;
        }
        const u32 w = m_viewport->RenderWidth();
        const u32 h = m_viewport->RenderHeight();
        if (w == 0 || h == 0)
        {
            return;
        }
        frame.encoder->TransitionTexture(m_viewport->ColorTexture(), m_viewport->ColorState(),
                                         rhi::ResourceState::RenderTarget);
        render->RenderOverlays(*frame.encoder, m_viewport->ColorTargetView(),
                               m_viewport->ColorFormat(), w, h, frame.frameIndex);
        frame.encoder->TransitionTexture(m_viewport->ColorTexture(),
                                         rhi::ResourceState::RenderTarget,
                                         rhi::ResourceState::ShaderRead);
        m_viewport->SetColorState(rhi::ResourceState::ShaderRead);
    }

    void GameEditorPage::OnClose()
    {
        Stop();
        // Stop leaves this page's viewport source as the input override (by design, so a STOPPED-
        // but-open tab keeps viewport input). On CLOSE the source is about to be freed, so clear it
        // or ActiveSource() dangles and the next PumpInput crashes (guarded: only if it's ours).
        if (m_input != nullptr)
        {
            m_input->ClearSourceProviderIf(&m_viewportSource);
        }
        // The PER-INSTANCE input source (BindInput set it to this tab's viewport) also dangles once
        // m_viewportSource is freed - and ReleaseInstance below is a NO-OP for the PRIMARY instance,
        // so its DriveInput would dereference the freed source next frame. Revert it to the shell.
        if (m_gameInstance != nullptr)
        {
            m_gameInstance->SetInputSource(m_input != nullptr ? &m_input->ShellSource() : nullptr);
        }
        // Destroy THIS tab's extra instance (unregisters its scene manager + tears down its run
        // host) so nothing dangling is ticked/rendered after the tab closes. No-op for the primary.
        if (m_app != nullptr && m_gameInstance != nullptr)
        {
            m_app->ReleaseInstance(m_gameInstance);
        }
        m_gameInstance = nullptr;
        m_context->StopGameRun = Function<void()>{};
        m_viewport->Shutdown();
    }

    void GameEditorPage::EnsureCamera(scene::Scene& scene)
    {
        auto* cameras = scene.GetSystem<engine::render::CameraComponentManager>();
        if (cameras == nullptr)
        {
            return;
        }
        bool hasCamera = false;
        cameras->ForEach([&](engine::render::CameraComponent&, scene::EntityHandle) { hasCamera = true; });
        if (hasCamera)
        {
            return;
        }

        LOG_WARNING(u8"Editor", u8"Game: scene has no camera - adding a default one");
        const scene::EntityHandle e = scene.CreateEntity(u8"PlayerCamera");
        Transform t;
        t.position = Float3{8.0f, 6.0f, 10.0f};
        t.rotation = Quaternion::FromAxisAngle(Float3{0, 1, 0}, 0.675f) *
                     Quaternion::FromAxisAngle(Float3{1, 0, 0}, -0.42f);
        scene.SetLocalTransform(e, t);
        cameras->Add(e);
    }

    void GameEditorPage::BindInput()
    {
        if (m_input == nullptr)
        {
            return;
        }
        m_viewportSource.viewport = m_viewport.Get();
        m_viewportSource.shellInput = m_shellInput;
        // Per-surface scene binding: the viewport source represents
        // THIS run's scene, so game-UI routing + consumption confine to it - open
        // editing pages' HUDs can no longer catch the run's clicks/keys, and the
        // run's UI never reacts to another scene's coordinates.
        m_input->SetSourceProvider(&m_viewportSource,
                                   m_scene); // UI-pump active source (game-UI routing)
        // Per-instance INPUT: this tab's game reads its OWN viewport source through its OWN action
        // runtime, so two Game tabs never cross-feed keys (only the FOCUSED tab's surface reports
        // them). The shared subsystem runtime is no longer the game's input.
        if (m_gameInstance != nullptr)
        {
            m_gameInstance->SetInputSource(&m_viewportSource);
        }
        const Guid mapId = m_context->Project()->Settings().defaultInputMapId;
        if (mapId.IsNil() || m_context->Resources() == nullptr)
        {
            return;
        }
        auto proxy = m_context->Resources()->Bind<foundation::input::InputMapResource>(mapId);
        if (proxy)
        {
            if (m_gameInstance != nullptr)
            {
                m_gameInstance->SetInputMap(proxy->Map());
            }
            LOG_INFO(u8"Editor", u8"Game: input map bound ({} set(s))",
                              proxy->Map().sets.Size());
        }
        else
        {
            m_context->Notify(NoticeKind::Warning, u8"Game: default input map is not cooked yet.");
        }
    }

    void GameEditorPage::BindBusLayout(runtime::IApplicationHost& host)
    {
        auto* audio = host.Ctx().GetSubsystem<engine::audio::AudioSubsystem>();
        if (audio == nullptr || audio->Engine() == nullptr)
        {
            return;
        }
        const Guid layoutId = m_context->Project()->Settings().defaultBusLayoutId;
        if (layoutId.IsNil() || m_context->Resources() == nullptr)
        {
            return;
        }
        auto proxy =
            m_context->Resources()->Bind<foundation::audio::AudioBusLayoutResource>(layoutId);
        if (proxy)
        {
            audio->Engine()->ApplyBusLayout(proxy->layout);
            LOG_INFO(u8"Editor", u8"Game: audio bus layout applied");
        }
        else
        {
            m_context->Notify(NoticeKind::Warning, u8"Game: default bus layout is not cooked yet.");
        }
    }

    void GameEditorPage::StartGameScriptFromProject()
    {
        // The startup script is a cooked ScriptClass asset (guid-authoritative), bound from the
        // content DB like every other asset - no raw source path.
        const Guid scriptId = m_context->Project()->Settings().startupScriptId;
        if (scriptId.IsNil() || m_context->Resources() == nullptr)
        {
            return;
        }
        auto proxy = m_context->Resources()->Bind<foundation::script::ScriptClass>(scriptId);
        if (!proxy || proxy->source.IsEmpty())
        {
            m_context->Notify(NoticeKind::Warning, u8"Game: startup script asset not found.");
            return;
        }
        if (m_gameInstance == nullptr ||
            !m_gameInstance->StartScript(
                proxy->source.AsView(), proxy->sourceName.AsView(),
                foundation::core::Span<const foundation::core::String>(proxy->handlers.Data(),
                                                                       proxy->handlers.Size())))
        {
            m_context->Notify(NoticeKind::Error,
                              u8"Game: startup script failed to start (see Console).");
        }
    }

    void GameEditorPage::EnableDebugging()
    {
        if (m_gameInstance == nullptr)
        {
            return;
        }
        m_gameInstance->RunHost().SetExternalDebugListener(&m_debugListener);
        EditorContext* context = m_context;
        GameEditorPage* self = this;
        m_gameInstance->RunHost().RequestDebugger(Function<void(script::IScriptDebugger&)>{
            [context, self](script::IScriptDebugger& debugger)
            {
                for (const EditorContext::ScriptBreakpoint& breakpoint : context->Breakpoints())
                {
                    debugger.SetBreakpoint(breakpoint.file.AsView(), breakpoint.line);
                    self->m_appliedBreakpoints.PushBack(breakpoint);
                }
                self->m_debuggerPanel.SetDebugger(&debugger);
            }});

        // LIVE breakpoint sync: gutter toggles during the run diff-apply onto the debugger
        // (removals take effect on the next executed line; additions arm immediately).
        m_context->OnBreakpointsChanged = [self] { self->SyncBreakpointsToDebugger(); };

        // Hover-value probe for ScriptPages: while THIS run is paused at a breakpoint, an
        // identifier resolves against the innermost frame's locals. Cleared on Stop.
        m_context->ScriptValueProbe = [self](StringView identifier) -> String
        {
            if (self->m_gameInstance == nullptr || !self->m_running ||
                !self->m_gameInstance->RunHost().IsDebugPaused())
            {
                return String();
            }
            script::IScriptDebugger* debugger = self->m_debuggerPanel.Debugger();
            if (debugger == nullptr)
            {
                return String();
            }
            const Array<script::ScriptVariable> locals = debugger->CaptureLocals(0);
            for (const script::ScriptVariable& local : locals)
            {
                if (local.name.AsView() != identifier)
                {
                    continue;
                }
                String text(local.value.AsView());
                if (!local.typeName.IsEmpty())
                {
                    text.Append(u8" : ");
                    text.Append(local.typeName.AsView());
                }
                return text;
            }
            return String();
        };
    }

    void GameEditorPage::DrainDebuggerState()
    {
        if (!m_running)
        {
            return;
        }
        if (m_debugListener.changed)
        {
            m_debugListener.changed = false;
            const bool paused = m_debugListener.state == script::ScriptDebuggerState::Breakpoint ||
                                m_debugListener.state == script::ScriptDebuggerState::Stepped;
            if (paused)
            {
                if (m_scene != nullptr && !m_simPausedByDebugger)
                {
                    m_scene->SetSimulationEnabled(false);
                    m_simPausedByDebugger = true;
                }
                m_debuggerPanel.Refresh();
                // Publish the paused location (innermost frame) - the ScriptPage editing
                // that file shows it as the ExecutionLine marker.
                if (script::IScriptDebugger* debugger = m_debuggerPanel.Debugger())
                {
                    const Array<script::ScriptStackFrame> frames =
                        debugger->CaptureStackFrames();
                    if (!frames.IsEmpty())
                    {
                        m_context->SetScriptExecutionPoint(frames[0].file.AsView(),
                                                           frames[0].line);
                    }
                }
            }
            else
            {
                if (m_scene != nullptr && m_simPausedByDebugger)
                {
                    m_scene->SetSimulationEnabled(true);
                    m_simPausedByDebugger = false;
                }
                m_debuggerPanel.Clear();
                m_context->ClearScriptExecutionPoint();
            }
        }
        if (m_debuggerPanel.ConsumeDirty())
        {
            m_debuggerPanel.Refresh();
        }
    }

    void GameEditorPage::SyncBreakpointsToDebugger()
    {
        script::IScriptDebugger* debugger = m_debuggerPanel.Debugger();
        if (debugger == nullptr || !m_running)
        {
            return;
        }
        const Span<const EditorContext::ScriptBreakpoint> store = m_context->Breakpoints();
        const auto contains = [](Span<const EditorContext::ScriptBreakpoint> set,
                                 const EditorContext::ScriptBreakpoint& breakpoint)
        {
            for (const EditorContext::ScriptBreakpoint& entry : set)
            {
                if (entry.line == breakpoint.line &&
                    entry.file.AsView() == breakpoint.file.AsView())
                {
                    return true;
                }
            }
            return false;
        };
        for (const EditorContext::ScriptBreakpoint& applied : m_appliedBreakpoints)
        {
            if (!contains(store, applied))
            {
                debugger->RemoveBreakpoint(applied.file.AsView(), applied.line);
            }
        }
        const Span<const EditorContext::ScriptBreakpoint> appliedView(
            m_appliedBreakpoints.Data(), m_appliedBreakpoints.Size());
        for (const EditorContext::ScriptBreakpoint& breakpoint : store)
        {
            if (!contains(appliedView, breakpoint))
            {
                debugger->SetBreakpoint(breakpoint.file.AsView(), breakpoint.line);
            }
        }
        m_appliedBreakpoints.Clear();
        for (const EditorContext::ScriptBreakpoint& breakpoint : store)
        {
            m_appliedBreakpoints.PushBack(breakpoint);
        }
    }

    void GameEditorPage::CycleResolution()
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
        default:
            break;
        }
    }

    void GameEditorPage::RefreshToolbar()
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
    void GameScriptErrorSink::OnError(const foundation::script::ScriptError& error)
    {
        if (context == nullptr)
        {
            return;
        }
        String message(u8"Game script error: ");
        message += error.message;
        context->Notify(NoticeKind::Error, message.AsView());
    }
    foundation::shell::IKeyboard* GameViewportInputSource::Keyboard()
    {
        return viewport != nullptr ? viewport->Keyboard() : nullptr;
    }

    foundation::shell::IMouse* GameViewportInputSource::Mouse()
    {
        return viewport != nullptr ? viewport->Mouse() : nullptr;
    }

    i32 GameViewportInputSource::GamepadCount() const
    {
        // Count is structural; the surface's per-pad facades gate the actual reads.
        return shellInput != nullptr ? Min(shellInput->GamepadCount(), 8) : 0;
    }

    foundation::shell::IGamepad* GameViewportInputSource::Gamepad(i32 index)
    {
        auto* surface = viewport != nullptr ? viewport->Surface() : nullptr;
        return surface != nullptr ? surface->Gamepad(index) : nullptr;
    }

    foundation::shell::ITouch* GameViewportInputSource::Touch()
    {
        return viewport != nullptr ? viewport->Touch() : nullptr;
    }

    Span<const foundation::shell::InputEvent> GameViewportInputSource::Events()
    {
        // Key/text events stream only while the viewport owns keyboard focus - the
        // same gate SurfaceKeyboard applies to the polled reads.
        auto* surface = viewport != nullptr ? viewport->Surface() : nullptr;
        if (surface == nullptr || !surface->Focused() || shellInput == nullptr)
        {
            return {};
        }
        return shellInput->Events();
    }
}
