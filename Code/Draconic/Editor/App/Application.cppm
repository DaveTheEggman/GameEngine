// Draconic::EditorApp - :application partition.
//
// EditorApplication: the editor as a runtime IApplication (docs/design/editor.md §3.2) - the
// UISandbox wiring, assembled for real: TrueType font service + UIHost + RuntimeDockableWindowHost
// (floating panels = borderless OS windows, drag-follow Tick) + the EditorShell chrome on the main
// window, with the EditorContext + EditorProject from draconic.editor.core underneath. Opens (or
// scaffolds) the project directory on startup, restores the per-user dock layout, saves it on
// shutdown. Phase 1: chrome + project only; pages/panels grow in later phases.

module;
#define _CRT_SECURE_NO_WARNINGS
#include "Core/Prelude.h"
#include <cstdlib>

export module draconic.editor.app:application;

import draconic.core;
import draconic.shell;
import draconic.graphics;
import draconic.fonts;
import draconic.fonts.ttf;
import draconic.runtime;
import draconic.runtime.client;
import draconic.render.api;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.ui.runtime;
import draconic.ui.application;
import draconic.content;
import draconic.vfs;
import draconic.resource;
import draconic.editor;
import draconic.editor.core;
import draconic.settings;
import :assets_view;
import :editor_icons;
import :settings_dialog;
import :shell;
import :ui_page;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace rt = draconic::runtime;
    namespace graphics = draconic::graphics;
    namespace fonts = draconic::fonts;
    namespace uirt = draconic::ui::runtime;
    namespace ed = draconic::editor;
    namespace uiapp = draconic::ui::application;

    class EditorApplication;

    struct EditorAppConfig
    {
        String projectDirectory;               // opened on startup; scaffolded if no manifest yet
        String projectName = String(u8"Untitled");   // name used when scaffolding
        String fontPath;                       // UI font (.ttf); empty = no text (debug only)

        // Log capture registered on GlobalLogger by main() BEFORE anything else runs, so early
        // startup logs reach the console panel. Borrowed; main owns it (outlives the app).
        draconic::editor::EditorLogBuffer* logBuffer = nullptr;

        // Smoke-test aid: request a CLEAN shutdown after this many seconds (0 = never).
        // Exercises the real teardown path, unlike killing the process.
        f32 autoExitSeconds = 0.0f;
        // Smoke-test aid: trigger Build > Rebuild All after this many seconds (0 = never).
        // Exercises the hot-reload cascade exactly like the menu click.
        f32 autoRebuildSeconds = 0.0f;

        // The assembly seams (design doc §3.1) - editor.app never links engine modules or the
        // draconic.<sys>.editor plugin modules; the EXECUTABLE composes them here:
        /// Called from IApplication::Configure - register engine subsystems (scene/render/...).
        Function<void(rt::IApplicationHost&)> configureEngine;
        /// Called at the end of OnStartup - per-subsystem RegisterEditor entry points, plus
        /// wiring the app to engine INTERFACES it drives (app.SetSceneRenderer(...)).
        Function<void(EditorApplication&, rt::IApplicationHost&, uirt::UIHost&)> registerEditors;
    };

    class EditorApplication : public rt::IApplication
    {
    public:
        explicit EditorApplication(EditorAppConfig config) : m_config(Move(config)) {}

        [[nodiscard]] draconic::editor::EditorContext& Context() noexcept { return m_context; }
        [[nodiscard]] draconic::editor::EditorProject* Project() const noexcept { return m_project.Get(); }
        [[nodiscard]] EditorShell& Shell() noexcept { return m_shell; }
        /// The exe registers every engine builder here (from registerEditors), mirroring the
        /// RaptorCook CLI's set - the cook service routes through it.
        [[nodiscard]] draconic::editor::BuilderRegistry& Builders() noexcept { return m_builders; }
        [[nodiscard]] draconic::editor::EditorCookService& CookService() noexcept { return m_cookService; }

        /// The exe registers runtime resource factories here (from registerEditors); the app
        /// owns them + the ResourceManager over the project's cooked DB.
        void AddResourceFactory(UniquePtr<draconic::resource::IResourceFactory> factory)
        {
            if (!factory) { return; }
            if (m_resources) { m_resources->AddFactory(factory.Get()); }
            m_resourceFactories.PushBack(Move(factory));
        }
        [[nodiscard]] draconic::resource::ResourceManager* Resources() const noexcept { return m_resources.Get(); }

        void Configure(rt::IApplicationHost& host) override
        {
            if (m_config.configureEngine) { m_config.configureEngine(host); }
        }

        /// The renderer interface the app drives its per-frame scene bracket through (injected
        /// by the exe from registerEditors; null = no scene rendering). Borrowed.
        void SetSceneRenderer(draconic::render::ISceneRenderer* renderer) noexcept
        {
            m_sceneRenderer = renderer;
        }

        void OnStartup(rt::IApplicationHost& host) override
        {
            m_host = &host;
            graphics::RenderWindow* mainRw = host.MainRenderWindow();
            if (mainRw == nullptr) { return; }

            // Fonts (CPU rasterization; no device needed).
            m_fontService = MakeUnique<fonts::TrueTypeFontService>(DefaultAllocator());
            if (!m_config.fontPath.IsEmpty())
            {
                fonts::FontLoadOptions options = fonts::FontLoadOptions::ExtendedLatin();
                // A full ramp so styles can pick small (property fields), regular, and
                // heading sizes without falling back to a mismatched rasterization.
                const f32 sizes[] = { 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 16.0f, 18.0f, 20.0f, 24.0f, 32.0f };
                for (f32 size : sizes)
                {
                    options.pixelHeight = size;
                    (void)m_fontService->LoadFont(u8"Roboto", m_config.fontPath.AsView(), options);
                }
            }

            LoadEditorSettings();              // per-user prefs (templates root, ...); absent on first run
            EditorIcons::Get().Initialize();   // shared SVG drawables (toolbar + asset types)
            m_uiHost = MakeUnique<uirt::UIHost>(DefaultAllocator(), *host.Graphics(), *host.Shell(), *m_fontService);
            m_dockHost = MakeUnique<uiapp::RuntimeDockableWindowHost>(DefaultAllocator(), host, *m_uiHost);

            // Theme: register the toolkit extension BEFORE creating the stylesheet (extensions
            // only apply to themes built afterward), then the editor defaults to dark.
            draconic::ui::ThemeRegistry::RegisterExtension(&m_toolkitTheme);
            // Editor theme: the warm "Graphite & Orange" palette on the rounded theme (soft corners
            // everywhere) - a crafted, less-bland alternative to the stock flat/square cool-grey dark.
            m_styleSheet = draconic::ui::RoundedDarkTheme::Create(draconic::ui::ThemePalette::GraphiteOrange());
            // Editor-specific overrides on top of the stock theme: property-grid fields read
            // better noticeably smaller and tighter than the theme's 14px/6x4 control chrome
            // (a full inspector column of them is the densest text in the editor).
            m_styleSheet->ForClass(u8"property-field")
                .Set(draconic::ui::StyleProperty::FontSize, 12.0f)
                .Set(draconic::ui::StyleProperty::Padding, draconic::ui::Thickness{ 5, 2 });
            m_uiHost->Context().SetStyleSheet(m_styleSheet);

            // Exit goes through the dirty check: the shell consults this before honoring the
            // main window's close button / OS quit; File>Exit routes through the same helper.
            host.Shell()->OnMainWindowCloseRequested = [this]() { return ConfirmExitAllowed(); };

            m_shell.Build(m_context, m_dockHost.Get(), mainRw->Window().Width(), mainRw->Window().Height());
            // The ACTIVE page follows dock-tab activation, not just OpenPage/ClosePage - with
            // side-by-side tab groups, Save was hitting whichever page opened last, not the tab
            // the user selected. Non-page panels (Console, Assets) leave the active page alone.
            m_shell.Docks()->OnPanelActivated.Add(
                draconic::ui::Event<void(tk::DockablePanel*)>::Handler{ [this](tk::DockablePanel* panel) {
                    if (panel == nullptr) { return; }
                    for (const PagePanel& entry : m_pagePanels)
                    {
                        if (entry.panel == panel) { m_context.SetActivePage(entry.page); return; }
                    }
                } });
            m_uiHost->AttachWindow(mainRw, RefPtr<draconic::ui::RootView>(m_shell.Root()));

            // Toast overlay on the main window root (input passes through outside the cards);
            // EditorContext::Notify routes here, and also mirrors to the status bar.
            m_toastHost = MakeRef<tk::ToastHost>(DefaultAllocator());
            m_shell.Root()->AddView(m_toastHost.Get());
            m_context.OnNotice = [this](ed::NoticeKind kind, StringView message) {
                ShowToast(kind, message);
                m_context.SetStatus(message);
            };

            OpenProject();
            if (m_project)
            {
                // Per-user pinned assets (browser + picker surface them first).
                (void)LoadFavorites(m_context, m_project->EditorStateRoot().AsView());
                m_context.OnFavoritesChanged = [this]() {
                    if (m_project)
                    {
                        (void)SaveFavorites(m_context, m_project->EditorStateRoot().AsView());
                    }
                };
            }

            // Per-subsystem editor plugins register here (page factories, creators, ...), and
            // the exe injects the engine interfaces the app drives (SetSceneRenderer).
            if (m_config.registerEditors) { m_config.registerEditors(*this, host, *m_uiHost); }

            // Cook service + the real Assets panel, once the project AND the exe-registered
            // builders both exist.
            if (m_project)
            {
                m_resources = MakeUnique<draconic::resource::ResourceManager>(DefaultAllocator(),
                    m_project->CookedDb());
                for (const auto& factory : m_resourceFactories) { m_resources->AddFactory(factory.Get()); }
                m_context.SetResources(m_resources.Get());
                m_cookService.Initialize(*m_project, m_builders);
                // Pages request re-cooks after saving builder-backed assets (materials etc.).
                m_context.OnCookRequested = [this](bool rebuild) { m_cookService.RequestCook(rebuild); };
                m_assetsView = MakeRef<AssetsView>(DefaultAllocator(), m_context, m_cookService);
                AssetsView* assets = m_assetsView.Get();
                m_assetsView->OnOpenInstance = [this](draconic::content::Instance& instance) {
                    (void)OpenInstancePage(instance);
                };
                m_assetsView->OnCreate = [this](const draconic::editor::EditorContext::AssetCreator& creator,
                                                draconic::content::Group* group) {
                    CreateAndOpen(creator, group);
                };
                // Delete-while-open policy: close-then-delete. Called from a mutation-queue
                // action (never mid-event-dispatch), so synchronous panel + page teardown is
                // safe here - the same pair of steps the tab close button triggers.
                m_assetsView->OnCloseInstancePage = [this](const Guid& id) {
                    for (usize i = 0; i < m_pagePanels.Size(); ++i)
                    {
                        if (m_pagePanels[i].page->InstanceId() == id)
                        {
                            tk::DockablePanel* panel = m_pagePanels[i].panel;
                            UIEditorPage* page = m_pagePanels[i].page;
                            m_shell.Docks()->ClosePanel(panel);
                            ClosePage(page);
                            return;
                        }
                    }
                };
                m_cookService.OnCookFinished = [this, assets]() {
                    assets->Rebuild();
                    // Result toast: failures are sticky (Console has the log); silent when the
                    // cook was a no-op (the watcher fires those constantly).
                    const usize failed = m_cookService.LastFailedCount();
                    const usize cooked = m_cookService.LastCookedCount();
                    if (failed > 0)
                    {
                        ShowToast(ed::NoticeKind::Error,
                                  Format(u8"Cook: {} failed, {} cooked (see Console).", failed, cooked).AsView());
                    }
                    else if (cooked > 0)
                    {
                        ShowToast(ed::NoticeKind::Success,
                                  Format(u8"Cook finished: {} asset(s).", cooked).AsView());
                    }
                    // Hot reload: rebuilt products swap in behind the proxy handles - live
                    // scenes see the new resources with no reopen (dependents reload
                    // transitively through the manager's recorded edges).
                    if (m_resources)
                    {
                        for (const Guid& product : m_cookService.LastCookedProducts())
                        {
                            (void)m_resources->Reload(product);
                        }
                    }
                };
                m_shell.SetAssetsContent(m_assetsView.Get());
            }

            // Menus AFTER registration - File > New builds from the creator registry.
            BuildMenus();

            // Reopen the pages from the last session (falling back to the default document),
            // THEN restore the dock layout so page panels land back in their arrangement.
            if (m_project)
            {
                Array<Guid> pages;
                Guid activePage;
                if (LoadOpenPages(m_project->EditorStateRoot().AsView(), pages, activePage).IsOk())
                {
                    UIEditorPage* toActivate = nullptr;
                    for (const Guid& id : pages)
                    {
                        if (draconic::content::Instance* instance = m_project->SourceDb().GetInstance(id))
                        {
                            UIEditorPage* page = OpenInstancePage(*instance);
                            if (page != nullptr && id == activePage) { toActivate = page; }
                        }
                    }
                    if (toActivate != nullptr) { m_context.SetActivePage(toActivate); }
                }
                else
                {
                    // No saved page set (first launch): fall back to the default scene -
                    // guid first (authoritative), path mirror for guid-less manifests.
                    draconic::content::Instance* instance = nullptr;
                    if (!m_project->Settings().defaultSceneId.IsNil())
                    {
                        instance = m_project->SourceDb().GetInstance(m_project->Settings().defaultSceneId);
                    }
                    if (instance == nullptr && !m_project->Settings().defaultScene.IsEmpty())
                    {
                        instance = m_project->SourceDb().GetInstance(m_project->Settings().defaultScene.AsView());
                    }
                    if (instance != nullptr) { (void)OpenInstancePage(*instance); }
                }
                (void)m_shell.RestoreLayout(m_project->EditorStateRoot().AsView());
            }
        }

        /// Open (or focus) a page for `instance` and dock its content as a center tab.
        UIEditorPage* OpenInstancePage(draconic::content::Instance& instance)
        {
            const usize before = m_context.OpenPages().Size();
            draconic::editor::EditorPage* page = m_context.OpenPage(instance);
            if (page == nullptr)
            {
                m_context.SetStatus(u8"No editor registered for this asset type.");
                return nullptr;
            }
            // All factories in this app produce UIEditorPages (:ui_page contract).
            UIEditorPage* uiPage = static_cast<UIEditorPage*>(page);
            if (m_context.OpenPages().Size() == before)
            {
                // Focused an existing page - select its tab.
                for (const PagePanel& entry : m_pagePanels)
                {
                    if (entry.page == uiPage) { m_shell.Docks()->ActivatePanel(entry.panel); break; }
                }
                return uiPage;
            }

            tk::DockablePanel* panel = m_shell.AddPagePanel(uiPage->Title(), uiPage->ContentView());
            {
                // Guid-keyed persistence id: the saved dock layout re-places this page's panel
                // when the page reopens on the next launch.
                utf8char guidChars[37];
                uiPage->InstanceId().ToChars(guidChars);
                panel->SetPersistenceId(StringView(guidChars));
            }
            // (Docking activates the new tab - toolkit behavior since the dock-activates change.)
            // The DockManager's own close handling (wired in AddPanel) destroys the panel through
            // its deferred-delete queue; we additionally tear down the PAGE - deferred through the
            // UI mutation queue, since destroying views mid-event-dispatch is unsafe.
            panel->OnCloseRequested.Add([this, uiPage](tk::DockablePanel*) {
                m_uiHost->Context().MutationQueueRef().QueueAction(
                    Function<void()>{ [this, uiPage]() { ClosePage(uiPage); } });
            });
            // Dirty pages don't close silently: veto the gesture and prompt Save / Discard /
            // Cancel. The dialog's buttons invoke OnCloseRequested DIRECTLY (bypassing this
            // veto), which runs the normal dock + page teardown.
            panel->OnCloseInterceptor = [this, uiPage](tk::DockablePanel* p) -> bool {
                if (!uiPage->IsDirty()) { return true; }
                ShowDirtyCloseDialog(uiPage, p);
                return false;
            };
            m_pagePanels.PushBack(PagePanel{ uiPage, panel });
            return uiPage;
        }

        // Tear down a page whose panel is closing/closed (the DockManager owns panel
        // destruction; this handles only the page side).
        /// Maps a context notice to a toast (errors stick until closed; the rest self-expire).
        void ShowToast(ed::NoticeKind kind, StringView message)
        {
            if (m_toastHost.Get() == nullptr) { return; }
            tk::ToastRequest request;
            request.message = String(message);
            switch (kind)
            {
                case ed::NoticeKind::Success: request.severity = tk::ToastSeverity::Success; break;
                case ed::NoticeKind::Warning: request.severity = tk::ToastSeverity::Warning; break;
                case ed::NoticeKind::Error:   request.severity = tk::ToastSeverity::Error; break;
                case ed::NoticeKind::Info:
                default:                      request.severity = tk::ToastSeverity::Info; break;
            }
            request.durationSeconds = (kind == ed::NoticeKind::Error) ? 0.0f : 5.0f;   // errors stick
            (void)m_toastHost->Show(Move(request));
        }

        void SaveActivePage()
        {
            auto* page = m_context.ActivePage();
            if (page == nullptr) { return; }
            if (page->Save().IsOk())
            {
                String message(u8"Saved '");
                message += page->Title();
                message += u8"'.";
                m_context.Notify(ed::NoticeKind::Success, message.AsView());
            }
            else
            {
                m_context.Notify(ed::NoticeKind::Error, u8"Save FAILED (see Console).");
            }
        }

        void ClosePage(UIEditorPage* page)
        {
            for (usize i = 0; i < m_pagePanels.Size(); ++i)
            {
                if (m_pagePanels[i].page == page)
                {
                    if (page->IsDirty())
                    {
                        m_context.SetStatus(u8"Closed page had unsaved changes.");   // save-prompt = later phase
                    }
                    page->OnClose();   // release GPU/scene resources while device + window live
                    m_pagePanels.RemoveAt(i);
                    m_context.ClosePage(page);   // destroys the page
                    return;
                }
            }
        }

        void OnUpdate(rt::IApplicationHost& host, f32 dt) override
        {
            if (m_config.autoExitSeconds > 0.0f || m_config.autoRebuildSeconds > 0.0f)
            {
                m_elapsed += dt;
                if (m_config.autoExitSeconds > 0.0f && m_elapsed >= m_config.autoExitSeconds)
                {
                    host.Shell()->RequestExit();
                }
                if (m_config.autoRebuildSeconds > 0.0f && !m_autoRebuilt
                    && m_elapsed >= m_config.autoRebuildSeconds)
                {
                    m_autoRebuilt = true;
                    m_cookService.RequestCook(true);
                }
            }
            // Headless-debug hook: RAPTOR_TEST_OPEN=<guid> opens that instance's page ~2s in
            // and opens it AGAIN ~4s in (the focus-existing branch) - reproduces the asset
            // browser's double-click paths in unattended (ASAN/gdb) runs.
            if (const char* testOpen = std::getenv("RAPTOR_TEST_OPEN"); testOpen != nullptr && m_project)
            {
                m_testOpenElapsed += dt;
                const bool first  = m_testOpenStage == 0 && m_testOpenElapsed >= 2.0f;
                const bool second = m_testOpenStage == 1 && m_testOpenElapsed >= 4.0f;
                if (first || second)
                {
                    ++m_testOpenStage;
                    Guid id;
                    if (Guid::TryParse(StringView(reinterpret_cast<const utf8char*>(testOpen)), id))
                    {
                        if (draconic::content::Instance* instance = m_project->SourceDb().GetInstance(id))
                        {
                            (void)OpenInstancePage(*instance);
                        }
                    }
                }
            }

            // Headless-debug hook: RAPTOR_TEST_REIMPORT="<group>;<file>" deletes the named
            // source group ~2s in and reimports <file> ~4s in (the watcher recook follows) -
            // scripts the delete->reimport crash repro for unattended ASAN runs.
            if (const char* reimport = std::getenv("RAPTOR_TEST_REIMPORT"); reimport != nullptr && m_project)
            {
                m_testOpenElapsed += dt;   // shared timer with RAPTOR_TEST_OPEN (use one hook per run)
                const StringView spec(reinterpret_cast<const utf8char*>(reimport));
                usize semi = spec.Size();
                for (usize i = 0; i < spec.Size(); ++i) { if (spec.Data()[i] == u8';') { semi = i; break; } }
                if (semi < spec.Size())
                {
                    if (m_testOpenStage == 0 && m_testOpenElapsed >= 2.0f)
                    {
                        ++m_testOpenStage;
                        const String groupName(spec.SubStr(0, semi));
                        if (draconic::content::Group* group =
                                m_project->SourceDb().RootGroup()->GetGroup(groupName.AsView()))
                        {
                            m_cookService.RunWhenIdle(Function<void()>{ [this, group]() {
                                (void)m_project->SourceDb().DeleteGroup(*group);
                                m_context.SetStatus(u8"[test] deleted group");
                                // Mirror DeleteGroupNow: the assets tree holds raw Group*
                                // rows - EVERY source-DB group mutation must Rebuild before
                                // the next layout binds stale pointers.
                                if (m_assetsView) { m_assetsView->Rebuild(); }
                            } });
                        }
                    }
                    else if (m_testOpenStage == 1 && m_testOpenElapsed >= 4.0f)
                    {
                        ++m_testOpenStage;
                        const String file(spec.SubStr(semi + 1, spec.Size() - semi - 1));
                        m_context.SetStatus(u8"[test] reimporting");
                        if (m_assetsView) { m_assetsView->ImportFile(file.AsView()); }
                    }
                }
            }

            DrainLog();
            SyncPageTitles();
            // Background-cook progress -> status bar (log lines reach the Console via the
            // logger); a finished cook refreshes the Assets badges through OnCookFinished.
            m_cookService.Update(Function<void(StringView)>{ [this](StringView line) {
                m_context.SetStatus(line);
            } });

            // Background jobs: pump, drain job logs, and once an export's pre-cook has finished, submit
            // the export's pack/stage job. Show the running job's step + percent in the status bar.
            m_jobService.Update(Function<void(StringView)>{ [this](StringView line) {
                m_context.SetStatus(line);
            } });
            if (m_pendingExport.active && m_pendingExport.waitingCook && !m_cookService.IsCooking())
            {
                m_pendingExport.waitingCook = false;
                SubmitExportJob(m_pendingExport.presetName, m_pendingExport.all);
                m_pendingExport.active = false;   // the job owns it now
            }
            if (m_jobService.IsBusy())
            {
                const ed::EditorJobService::ProgressView p = m_jobService.Progress();
                if (p.active)
                {
                    String s(p.title.AsView());
                    if (!p.step.IsEmpty()) { s += u8": "; s += p.step; }
                    s += u8" ("; AppendCountTo(s, static_cast<usize>(p.fraction * 100.0f + 0.5f)); s += u8"%)";
                    m_context.SetStatus(s.AsView());
                }
            }

            if (m_assetsView) { m_assetsView->Refresh(); }
            if (m_resources) { m_resources->CollectGarbage(); }   // release hot-reloaded-away products

            // OS file drops -> the import pipeline (any editor window; imports land in the
            // Assets panel's selected group).
            if (m_assetsView && host.Shell() != nullptr)
            {
                m_droppedFiles.Clear();
                host.Shell()->DrainDroppedFiles(m_droppedFiles);
                for (const draconic::shell::DroppedFile& drop : m_droppedFiles)
                {
                    m_assetsView->ImportFile(drop.path.AsView());
                }
            }
            if (m_uiHost) { m_uiHost->Update(dt); }
            if (m_toastHost) { m_toastHost->Update(dt); }
            if (m_dockHost) { m_dockHost->Tick(); }   // drag-follow for floating OS windows

            // Page hooks AFTER the UI laid out (viewport rects are current for input gating).
            for (const PagePanel& entry : m_pagePanels) { entry.page->OnUpdate(host, dt); }
        }

        void OnRenderWindow(rt::IApplicationHost& host, graphics::FrameContext& frame) override
        {
            // ALL pages' viewport content renders during the MAIN window's frame, inside ONE
            // scene-renderer bracket, BEFORE any UI draws (the Sedulous editor structure:
            // offscreen targets are window-agnostic, so floated panels' windows simply sample
            // the textures this pass produced). Secondary-window frames are UI-only.
            if (frame.valid && frame.window == host.MainRenderWindow())
            {
                // Through the ISceneRenderer INTERFACE (render.api) - editor.app never links the
                // renderer. Begin/EndRendering self-guard while the renderer isn't ready.
                if (m_sceneRenderer != nullptr) { m_sceneRenderer->BeginRendering(*frame.encoder, frame.frameIndex); }
                for (const PagePanel& entry : m_pagePanels) { entry.page->OnRenderWindow(host, frame); }
                if (m_sceneRenderer != nullptr) { m_sceneRenderer->EndRendering(); }
            }
            if (m_uiHost) { m_uiHost->RenderWindow(frame); }
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            m_cookService.Shutdown();   // joins any in-flight cook before the DBs go away
            // Release page resources while the device and windows are still alive.
            for (const PagePanel& entry : m_pagePanels) { entry.page->OnClose(); }
            SaveLayout();
            EditorIcons::Get().Shutdown();   // release drawables deterministically
        }

    private:
        // File > New <creator>: create the source instance, remember it as the project's default
        // document if none is set yet (so a fresh project reopens where you left off), open it.
        void CreateAndOpen(const draconic::editor::EditorContext::AssetCreator& creator,
                           draconic::content::Group* group = nullptr)
        {
            // Cook gate: the plan worker reads the DBs with their structure frozen -
            // creating instances mid-plan is a race. Queue and replay when idle.
            if (m_cookService.IsCooking())
            {
                const draconic::editor::EditorContext::AssetCreator* entry = &creator;
                m_cookService.RunWhenIdle(Function<void()>{ [this, entry, group]() {
                    CreateAndOpen(*entry, group);
                } });
                m_context.Notify(ed::NoticeKind::Info,
                                 u8"Create queued until the current cook finishes.");
                return;
            }
            draconic::content::Instance* instance = creator.create(m_context, group);
            if (instance == nullptr)
            {
                m_context.SetStatus(u8"Create failed (no project open?).");
                return;
            }
            if (creator.setsDefaultScene && m_project && m_project->Settings().defaultSceneId.IsNil()
                && m_project->Settings().defaultScene.IsEmpty())
            {
                m_project->Settings().defaultSceneId = instance->Id();
                m_project->Settings().defaultScene = instance->Path();
                (void)m_project->SaveSettings();
            }
            // Surface the new row immediately (the File-menu path bypasses the assets view's own
            // rebuild) and cook it so builder-backed assets become pickable without a manual
            // Cook All (a no-op for builder-less scenes: nothing is dirty).
            if (m_assetsView) { m_assetsView->Rebuild(); }
            if (m_builders.FindByTypeName(instance->TypeName()) != nullptr)
            {
                m_cookService.RequestCook(false);
            }
            (void)OpenInstancePage(*instance);
        }

        // True = nothing dirty, exit may proceed. Otherwise shows the exit prompt and returns
        // false; its buttons finish the job (save-all -> exit / discard -> exit / cancel).
        [[nodiscard]] bool ConfirmExitAllowed()
        {
            usize dirtyCount = 0;
            for (const PagePanel& entry : m_pagePanels)
            {
                if (entry.page->IsDirty()) { ++dirtyCount; }
            }
            if (dirtyCount == 0) { return true; }

            String message;
            AppendCountTo(message, dirtyCount);
            message += (dirtyCount == 1) ? StringView(u8" page has unsaved changes.")
                                         : StringView(u8" pages have unsaved changes.");
            RefPtr<draconic::ui::Dialog> dialog =
                MakeRef<draconic::ui::Dialog>(DefaultAllocator(), StringView(u8"Unsaved changes"));
            RefPtr<draconic::ui::Label> label =
                MakeRef<draconic::ui::Label>(DefaultAllocator(), message.AsView());
            label->WordWrap.SetValue(true);
            dialog->SetContent(label.Get());

            draconic::ui::Dialog* rawDialog = dialog.Get();
            draconic::ui::Button* saveAll =
                dialog->AddButton(u8"Save All & Exit", draconic::ui::DialogResult::None);
            saveAll->OnClick.Add([this, rawDialog](draconic::ui::ButtonBase*) {
                bool allSaved = true;
                for (const PagePanel& entry : m_pagePanels)
                {
                    if (entry.page->IsDirty() && !entry.page->Save().IsOk()) { allSaved = false; }
                }
                if (allSaved) { m_host->RequestExit(); }
                else { m_context.SetStatus(u8"Save FAILED (see console) - staying open."); }
                rawDialog->Close(allSaved ? draconic::ui::DialogResult::OK
                                          : draconic::ui::DialogResult::Cancel);
            });
            draconic::ui::Button* discard =
                dialog->AddButton(u8"Exit Without Saving", draconic::ui::DialogResult::None);
            discard->OnClick.Add([this, rawDialog](draconic::ui::ButtonBase*) {
                m_host->RequestExit();
                rawDialog->Close(draconic::ui::DialogResult::OK);
            });
            dialog->AddButton(u8"Cancel", draconic::ui::DialogResult::Cancel);
            dialog->Show(&m_uiHost->Context());
            return false;
        }

        static void AppendCountTo(String& out, usize value)
        {
            utf8char digits[20];
            usize n = 0;
            do { digits[n++] = static_cast<utf8char>('0' + (value % 10)); value /= 10; } while (value != 0);
            while (n > 0) { out.PushBack(digits[--n]); }
        }

        // === Export ===

        // Build a fresh export template registry + presets and run one preset (or all) via the shared
        // driver - the SAME ExportOne/ExportAll the RaptorExport CLI calls. The host template comes
        // from this editor's own Bin dir (where RaptorPlayer + its .runtime-libs live). (Imported
        // cross-platform templates land with the templates-manager UI; host-platform export works now.)
        // Export runs in TWO safe phases so the UI never freezes: (1) cook through the CookService
        // (background + DB-safe - the cook mutates the DB the UI reads), then (2) once the cook finishes
        // (polled in OnUpdate), a background JobService job packs/stages/player (reader-only, cook=false).
        void RunExport(StringView presetName, bool all)
        {
            if (!m_project) { m_context.Notify(ed::NoticeKind::Info, u8"Open a project first."); return; }
            if (m_jobService.IsBusy() || m_pendingExport.active)
            {
                m_context.Notify(ed::NoticeKind::Info, u8"An export is already in progress.");
                return;
            }
            m_pendingExport = PendingExport{ String(presetName), all, /*waitingCook*/ true, /*active*/ true };
            m_context.Notify(ed::NoticeKind::Info, u8"Cooking before export...");
            m_cookService.RequestCook(false);   // safe background cook; OnUpdate fires the export job after it
        }

        // Phase 2: pack/stage/player as a background job (the cook already ran). File I/O only - no
        // source/cooked DB MUTATION - so it is safe alongside the main thread's DB reads.
        // Load per-user editor settings from <userdata>/editor.settings.xml (registering the section
        // types first). Absent on first run - the store stays empty and sections read as defaults.
        void LoadEditorSettings()
        {
            ed::RegisterEditorSettingsTypes();
            const String dir = GetUserDataDirectory(u8"draconic");
            draconic::vfs::NativeFileSystem fs(dir.AsView());
            (void)ed::LoadEditorSettings(fs, m_editorSettings);   // NotFound on first run is fine
        }

        // The editor's export templates root: the EditorExportSettings override when set, else
        // $DRACONIC_TEMPLATES_DIR, else the <user-data>/templates default (same order as the CLI).
        [[nodiscard]] String TemplatesRoot() const
        {
            StringView overrideRoot;
            if (const ed::EditorExportSettings* s = m_editorSettings.Find<ed::EditorExportSettings>())
            {
                overrideRoot = s->templatesRoot.AsView();
            }
            return ed::ResolveTemplatesRoot(overrideRoot);
        }

        void SubmitExportJob(String presetName, bool all)
        {
            draconic::editor::EditorProject* project = m_project.Get();
            draconic::editor::BuilderRegistry* builders = &m_builders;
            const String toolDir = GetExecutableDirectory();
            const String templatesRoot = TemplatesRoot();   // resolve on the main thread (reads settings)
            const String outRoot = PathJoin(m_project->Directory(), u8"Dist");
            const String title(all ? StringView(u8"Export All") : StringView(u8"Export"));

            m_jobService.Submit(title.AsView(),
                [project, builders, toolDir, templatesRoot, presetName, all, outRoot](ed::JobContext& ctx) -> Status
                {
                    draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
                    draconic::vfs::NativeFileSystem rootFs(templatesRoot.AsView());   // imported templates
                    ed::TemplateRegistry registry;
                    registry.Refresh(templatesRoot.AsView(), &rootFs, toolDir.AsView(), &toolFs);
                    ed::ExportPresetSet presets;
                    {
                        draconic::vfs::NativeFileSystem projectFs(project->Directory());
                        if (!ed::LoadExportPresets(projectFs, presets).IsOk()) { ed::DefaultExportPresets(presets); }
                    }
                    const ed::ExportProgress onProgress = [&ctx](StringView step, f32 frac)
                    { ctx.SetStep(step); ctx.SetFraction(frac); };

                    if (all)
                    {
                        const Span<const ed::ExportPreset> span(presets.presets.Data(), presets.presets.Size());
                        return ed::ExportAll(*project, span, registry, *builders, outRoot.AsView(),
                                             /*rebuild*/ false, onProgress, /*cook*/ false);
                    }
                    const ed::ExportPreset* preset = presets.Find(presetName.AsView());
                    if (preset == nullptr) { return Status{ ErrorCode::NotFound }; }
                    ed::ExportResult result;
                    return ed::ExportOne(*project, *preset, registry, *builders, outRoot.AsView(),
                                         /*rebuild*/ false, &result, onProgress, /*cook*/ false);
                },
                [this, outRoot](Status s)   // main thread
                {
                    if (!s.IsOk())
                    {
                        m_context.Notify(ed::NoticeKind::Error, u8"Export failed (see Console).");
                        return;
                    }
                    m_context.SetStatus(u8"Export complete.");
                    // Sticky success toast with a button that reveals the Dist folder in the OS file
                    // manager (the button click dismisses the toast, per ToastHost's onAction contract).
                    if (m_toastHost.Get() != nullptr)
                    {
                        tk::ToastRequest request;
                        request.message = String(u8"Export complete.");
                        request.severity = tk::ToastSeverity::Success;
                        request.durationSeconds = 0.0f;   // sticky
                        request.actionLabel = String(u8"Open Folder");
                        request.onAction = [this, outRoot]()
                        {
                            if (m_host != nullptr && m_host->Shell() != nullptr
                                && m_host->Shell()->Dialogs() != nullptr)
                            {
                                m_host->Shell()->Dialogs()->OpenPath(outRoot.AsView());
                            }
                        };
                        (void)m_toastHost->Show(Move(request));
                    }
                });
        }

        // Import an export template bundle: pick a folder (native dialog), copy it into the templates
        // root under its manifest id. The picked folder must contain a template.xml.
        void OpenImportTemplateDialog()
        {
            if (m_host == nullptr || m_host->Shell() == nullptr || m_host->Shell()->Dialogs() == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Error, u8"File dialogs are unavailable.");
                return;
            }
            m_host->Shell()->Dialogs()->ShowOpenFolder(
                Function<void(Span<const String>)>{ [this](Span<const String> paths)
                {
                    if (paths.Size() == 0) { return; }   // cancelled
                    const String root = TemplatesRoot();   // settings override / env / default
                    String id;
                    if (ed::ImportTemplate(paths[0].AsView(), root.AsView(), &id).IsOk())
                    {
                        String msg(u8"Imported template '"); msg += id; msg += u8"'.";
                        m_context.Notify(ed::NoticeKind::Success, msg.AsView());
                    }
                    else
                    {
                        m_context.Notify(ed::NoticeKind::Error,
                                         u8"Import failed - the folder has no valid template.xml.");
                    }
                } });
        }

        void OpenExportDialog()
        {
            if (!m_project) { m_context.Notify(ed::NoticeKind::Info, u8"Open a project first."); return; }

            ed::ExportPresetSet presets;
            {
                draconic::vfs::NativeFileSystem projectFs(m_project->Directory());
                if (!ed::LoadExportPresets(projectFs, presets).IsOk()) { ed::DefaultExportPresets(presets); }
            }

            auto dialog = MakeRef<draconic::ui::Dialog>(DefaultAllocator(), StringView(u8"Export"));
            auto column = MakeRef<draconic::ui::FlexLayout>(DefaultAllocator());
            column->Direction = draconic::ui::Orientation::Vertical;

            auto info = MakeRef<draconic::ui::Label>(DefaultAllocator(),
                StringView(u8"Export a preset (output: <project>/Dist):"));
            column->AddView(info.Get());

            draconic::ui::Dialog* raw = dialog.Get();
            for (const ed::ExportPreset& preset : presets.presets)
            {
                String label(u8"Export: "); label += preset.name;
                auto button = MakeRef<draconic::ui::Button>(DefaultAllocator(), label.AsView());
                const String name(preset.name.AsView());
                button->OnClick.Add([this, name, raw](draconic::ui::ButtonBase*) {
                    RunExport(name.AsView(), false);
                    raw->Close(draconic::ui::DialogResult::OK);
                });
                column->AddView(button.Get());
            }
            dialog->SetContent(column.Get());

            draconic::ui::Button* exportAll = dialog->AddButton(u8"Export All", draconic::ui::DialogResult::None);
            exportAll->OnClick.Add([this, raw](draconic::ui::ButtonBase*) {
                RunExport(StringView{}, true);
                raw->Close(draconic::ui::DialogResult::OK);
            });
            dialog->AddButton(u8"Close", draconic::ui::DialogResult::Cancel);
            dialog->Show(&m_uiHost->Context());
        }

        void ShowDirtyCloseDialog(UIEditorPage* page, tk::DockablePanel* panel)
        {
            String message(u8"'");
            message += page->Title();
            message += u8"' has unsaved changes.";
            RefPtr<draconic::ui::Dialog> dialog =
                MakeRef<draconic::ui::Dialog>(DefaultAllocator(), StringView(u8"Unsaved changes"));
            RefPtr<draconic::ui::Label> label =
                MakeRef<draconic::ui::Label>(DefaultAllocator(), message.AsView());
            label->WordWrap.SetValue(true);
            dialog->SetContent(label.Get());

            draconic::ui::Dialog* rawDialog = dialog.Get();
            draconic::ui::Button* save = dialog->AddButton(u8"Save", draconic::ui::DialogResult::None);
            save->OnClick.Add([this, page, panel, rawDialog](draconic::ui::ButtonBase*) {
                if (page->Save().IsOk())
                {
                    panel->OnCloseRequested.Invoke(panel);
                    rawDialog->Close(draconic::ui::DialogResult::OK);
                }
                else
                {
                    m_context.SetStatus(u8"Save FAILED (see console) - page stays open.");
                    rawDialog->Close(draconic::ui::DialogResult::Cancel);
                }
            });
            draconic::ui::Button* discard = dialog->AddButton(u8"Discard", draconic::ui::DialogResult::None);
            discard->OnClick.Add([panel, rawDialog](draconic::ui::ButtonBase*) {
                panel->OnCloseRequested.Invoke(panel);
                rawDialog->Close(draconic::ui::DialogResult::OK);
            });
            dialog->AddButton(u8"Cancel", draconic::ui::DialogResult::Cancel);
            dialog->Show(&m_uiHost->Context());
        }

        // Tab titles mirror dirty state (" *" suffix) - polled per frame; SetTitle no-ops
        // visually unless the string actually changed. The name comes from the LIVE instance
        // (the page captured it at open - a browser rename would leave the tab stale).
        void SyncPageTitles()
        {
            for (const PagePanel& entry : m_pagePanels)
            {
                String title;
                draconic::content::Instance* instance = m_project
                    ? m_project->SourceDb().GetInstance(entry.page->InstanceId()) : nullptr;
                if (instance != nullptr) { title = String(instance->Name()); }
                else { title = String(entry.page->Title()); }
                if (entry.page->IsDirty()) { title += u8" *"; }
                if (entry.panel->Title() != title.AsView()) { entry.panel->SetTitle(title.AsView()); }
            }
        }

        // Buffered engine logs -> the Console panel, once per frame on the main thread.
        void DrainLog()
        {
            if (m_config.logBuffer == nullptr || m_shell.Console() == nullptr) { return; }
            m_pendingLog.Clear();
            m_logSequence = m_config.logBuffer->CollectSince(m_logSequence, m_pendingLog);
            for (const draconic::editor::EditorLogEntry& entry : m_pendingLog)
            {
                m_shell.Console()->AddEntry(entry.level, entry.category.AsView(), entry.message.AsView());
            }
        }

        void OpenProject()
        {
            if (m_config.projectDirectory.IsEmpty())
            {
                m_context.SetStatus(u8"No project directory - pass one on the command line.");
                return;
            }

            m_project = draconic::editor::EditorProject::Open(m_config.projectDirectory.AsView());
            if (!m_project)
            {
                // No manifest yet: scaffold a fresh project, then open it.
                const Status created = draconic::editor::EditorProject::Create(
                    m_config.projectDirectory.AsView(), m_config.projectName.AsView());
                if (created.IsOk())
                {
                    m_project = draconic::editor::EditorProject::Open(m_config.projectDirectory.AsView());
                }
            }

            if (!m_project)
            {
                String message(u8"Failed to open project: ");
                message += m_config.projectDirectory;
                m_context.SetStatus(message.AsView());
                return;
            }

            m_context.SetProject(m_project.Get());

            // NOTE: the dock layout restores at the END of OnStartup, after the saved pages
            // reopen - page panels carry guid PersistenceIds, so their side-by-side/tabbed
            // arrangement only reconstitutes once the panels exist.

            String message(u8"Project: ");
            message += m_project->Name();
            message += u8"  (";
            message += m_project->Directory();
            message += u8")";
            m_context.SetStatus(message.AsView());
        }

        void SaveLayout()
        {
            if (m_project)
            {
                Array<Guid> pages;
                Guid activePage;
                for (const PagePanel& entry : m_pagePanels)
                {
                    pages.PushBack(entry.page->InstanceId());
                    if (m_context.ActivePage() == entry.page) { activePage = entry.page->InstanceId(); }
                }
                (void)SaveOpenPages(m_project->EditorStateRoot().AsView(), pages, activePage);
            }
            if (m_project && m_shell.Docks() != nullptr)
            {
                (void)m_shell.SaveLayout(m_project->EditorStateRoot().AsView());
            }
        }

        void BuildMenus()
        {
            tk::MenuBar* bar = m_shell.Menus();
            // Menu order: File FIRST (muscle memory), Build after it, Help-style menus last.
            draconic::ui::ContextMenu* file = bar->AddMenu(u8"File");
            if (draconic::ui::ContextMenu* build = bar->AddMenu(u8"Build"))
            {
                build->AddItem(u8"Cook All", [this]() { m_cookService.RequestCook(false); });
                build->AddItem(u8"Rebuild All", [this]() { m_cookService.RequestCook(true); });
            }

            if (file != nullptr)
            {
                rt::IApplicationHost* host = m_host;

                // File > New <creator> from the registry (per-subsystem editor modules).
                // Categorized creators (e.g. "Primitives") nest in a submenu of that name.
                Array<StringView> categories;
                for (const draconic::editor::EditorContext::AssetCreator& creator : m_context.Creators())
                {
                    if (creator.category.IsEmpty())
                    {
                        String label(u8"New ");
                        label += creator.label;
                        const auto* entry = &creator;
                        file->AddItem(label.AsView(), [this, entry]() { CreateAndOpen(*entry); });
                        continue;
                    }
                    bool seen = false;
                    for (StringView c : categories) { if (c == creator.category.AsView()) { seen = true; break; } }
                    if (!seen) { categories.PushBack(creator.category.AsView()); }
                }
                for (StringView category : categories)
                {
                    draconic::ui::MenuItem* submenuItem = file->AddSubmenu(category);
                    auto* submenu = Cast<draconic::ui::ContextMenu>(submenuItem->Submenu.Get());
                    if (submenu == nullptr) { continue; }
                    for (const draconic::editor::EditorContext::AssetCreator& creator : m_context.Creators())
                    {
                        if (creator.category.AsView() != category) { continue; }
                        const auto* entry = &creator;
                        submenu->AddItem(creator.label.AsView(), [this, entry]() { CreateAndOpen(*entry); });
                    }
                }
                if (!m_context.Creators().IsEmpty()) { file->AddSeparator(); }

                file->AddItem(u8"Save", [this]() { SaveActivePage(); });
                file->AddSeparator();
                file->AddItem(u8"Save Layout", [this]() {
                    SaveLayout();
                    m_context.SetStatus(u8"Layout saved.");
                });
                file->AddSeparator();
                file->AddItem(u8"Project Settings...", [this]() {
                    if (m_project)
                    {
                        auto dialog = MakeRef<ProjectSettingsDialog>(DefaultAllocator(), m_context);
                        dialog->Show(&m_uiHost->Context());
                    }
                });
                file->AddSeparator();
                file->AddItem(u8"Export...", [this]() { OpenExportDialog(); });
                file->AddItem(u8"Import Template...", [this]() { OpenImportTemplateDialog(); });
                file->AddItem(u8"Exit", [this, host]() {
                    if (host != nullptr && ConfirmExitAllowed()) { host->RequestExit(); }
                });
            }

            if (draconic::ui::ContextMenu* edit = bar->AddMenu(u8"Edit"))
            {
                edit->AddItem(u8"Undo", [this]() { m_context.Undo(); });
                edit->AddItem(u8"Redo", [this]() { m_context.Redo(); });
            }

            // Keyboard equivalents via the UI ShortcutManager. Shortcuts dispatch AFTER the
            // focused view, and text controls mark their key-downs handled - so a focused
            // textbox keeps Ctrl+Z for its own text undo and these fire everywhere else.
            draconic::ui::ShortcutManager* shortcuts = m_uiHost->Context().GetShortcuts();
            shortcuts->AddGlobal(draconic::ui::KeyCode::Z, draconic::ui::KeyModifiers::Ctrl,
                                 [this]() { m_context.Undo(); });
            shortcuts->AddGlobal(draconic::ui::KeyCode::Z,
                                 draconic::ui::KeyModifiers::Ctrl | draconic::ui::KeyModifiers::Shift,
                                 [this]() { m_context.Redo(); });
            shortcuts->AddGlobal(draconic::ui::KeyCode::Y, draconic::ui::KeyModifiers::Ctrl,
                                 [this]() { m_context.Redo(); });
            shortcuts->AddGlobal(draconic::ui::KeyCode::S, draconic::ui::KeyModifiers::Ctrl,
                                 [this]() { SaveActivePage(); });

            if (draconic::ui::ContextMenu* view = bar->AddMenu(u8"View"))
            {
                view->AddItem(u8"Reset Layout", [this]() {
                    m_shell.ResetLayout();
                    m_context.SetStatus(u8"Layout reset to default.");
                });
            }

            if (draconic::ui::ContextMenu* help = bar->AddMenu(u8"Help"))
            {
                help->AddItem(u8"About", [this]() {
                    m_context.SetStatus(u8"Draconic Editor - phase 1 shell (docs/design/editor.md)");
                });
            }
        }

        EditorAppConfig m_config;
        rt::IApplicationHost* m_host = nullptr;   // borrowed
        draconic::render::ISceneRenderer* m_sceneRenderer = nullptr;   // borrowed (exe injects)

        // Log drain state (see DrainLog).
        Array<draconic::editor::EditorLogEntry> m_pendingLog;
        u64 m_logSequence = 0;

        // Open pages and their center-tab panels (panels owned by the DockManager).
        struct PagePanel
        {
            UIEditorPage* page = nullptr;         // borrowed (context owns the page)
            tk::DockablePanel* panel = nullptr;   // borrowed (dock manager owns the panel)
        };
        draconic::editor::EditorContext m_context;
        UniquePtr<draconic::editor::EditorProject> m_project;
        draconic::editor::BuilderRegistry m_builders;        // exe-assembled (registerEditors)
        draconic::editor::EditorCookService m_cookService;
        draconic::editor::EditorJobService m_jobService;     // generic background jobs (export, ...)
        draconic::settings::Settings m_editorSettings;       // per-user editor prefs (<userdata>/editor.settings.xml)
        struct PendingExport { String presetName; bool all = false; bool waitingCook = false; bool active = false; };
        PendingExport m_pendingExport;                       // export waiting for its pre-cook to finish
        f32 m_elapsed = 0.0f;   // autoExit/autoRebuild accumulator
        f32 m_testOpenElapsed = 0.0f;   // RAPTOR_TEST_OPEN hook
        u32 m_testOpenStage = 0;
        bool m_autoRebuilt = false;
        Array<draconic::shell::DroppedFile> m_droppedFiles;   // per-frame drain buffer
        Array<UniquePtr<draconic::resource::IResourceFactory>> m_resourceFactories;   // exe-assembled
        UniquePtr<draconic::resource::ResourceManager> m_resources;

        UniquePtr<fonts::TrueTypeFontService> m_fontService;
        tk::ToolkitThemeExtension m_toolkitTheme;
        RefPtr<draconic::ui::StyleSheet> m_styleSheet;

        // TEARDOWN ORDER RULE: the UIHost (owns the UIContext + InputManager) is declared
        // BEFORE every view-holding member below, so it destructs AFTER them - view teardown
        // calls DetachView/Unregister on its context (LogView's ListView does, via
        // SetAdapter(nullptr) in its dtor), which is a use-after-free once the host is gone.
        // ASAN caught exactly that with the previous declared-last ordering.
        UniquePtr<uirt::UIHost> m_uiHost;
        UniquePtr<uiapp::RuntimeDockableWindowHost> m_dockHost;   // references m_uiHost: dies first
        EditorShell m_shell;
        RefPtr<AssetsView> m_assetsView;
        RefPtr<tk::ToastHost> m_toastHost;
        Array<PagePanel> m_pagePanels;
    };
}
