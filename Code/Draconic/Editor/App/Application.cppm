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
#include "Core/Log/Log.h"
#include <cstdlib>

export module draconic.editor.app:application;

import draconic.core;
import draconic.shell;
import draconic.graphics;
import draconic.fonts;
import draconic.fonts.ttf;
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.defaultapp;   // the embedded game application (v3)
import draconic.ui.resource;          // UITheme (the manifest's default game-UI theme)
import draconic.ui.subsystem;         // UISubsystem (SetDefaultTheme)
import draconic.input.subsystem;      // InputSubsystem (the embedded runtime's scene-input policy)
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
import :preferences_dialog;
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
    namespace ui = draconic::ui;

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
        /// The embedded game application (valid after OnStartup; the Game page drives its
        /// play bracket through it).
        [[nodiscard]] rt::DefaultApplication* EmbeddedApplication() const noexcept
        {
            return m_embeddedApp.Get();
        }

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

            // ---- the EMBEDDED RUNTIME (runtime-host.md v3) ----
            // The editor owns a second, persistent runtime Context populated by the SAME
            // DefaultApplication the player runs: gameplay subsystems live THERE, and every
            // scene (editing pages, Simulate, previews, the Game tab) is hosted there. The
            // editor's own context carries no gameplay subsystems. The editor's existing
            // ResourceManager is PRESET into the app (a second manager over the same cooked
            // DB would load every product twice), so it must exist first.
            if (m_project)
            {
                m_resources = MakeUnique<draconic::resource::ResourceManager>(DefaultAllocator(),
                    m_project->CookedDb());
                for (const auto& factory : m_resourceFactories) { m_resources->AddFactory(factory.Get()); }
                m_context.SetResources(m_resources.Get());
            }
            m_embeddedHost = MakeUnique<rt::EmbeddedApplicationHost>(DefaultAllocator(),
                host, m_runtimeContext);
            m_embeddedHost->SetExitHandler(Function<void(int)>{ [this](int code) {
                // "Exit" from embedded game code = stop the play session. DEFERRED to
                // after the page-update loop: the request usually fires from inside the
                // game script's update(), and Stop tears the script down.
                DRACONIC_LOG_INFO(u8"Editor", u8"embedded app requested exit({})", code);
                m_stopGameRequested = true;
            } });
            m_embeddedApp = MakeUnique<rt::DefaultApplication>(DefaultAllocator());
            if (!m_config.fontPath.IsEmpty()) { m_embeddedApp->SetUIFontPath(m_config.fontPath.AsView()); }
            if (m_resources) { m_embeddedApp->SetResourceManager(m_resources.Get()); }
            m_embeddedApp->Configure(*m_embeddedHost);
            // Embedded-runtime input policy (game-ui.md §9): UN-BOUND input must never
            // reach scene-tier game UI here. The player's shell source owns its whole
            // window, so its un-bound input reaches every scene (the AllScenes default);
            // in the editor the same rule would let raw shell keystrokes and editor-pane
            // clicks land in game canvases of OPEN EDITING PAGES. Editing/Simulate HUDs
            // therefore render WYSIWYG but are deliberately NOT interactive; the Game
            // tab is the interactive-run surface and binds its scene on Play.
            if (m_embeddedApp->Input() != nullptr)
            {
                m_embeddedApp->Input()->SetUnboundScenePolicy(
                    draconic::input::UnboundInputScenePolicy::ScreenTierOnly);
            }
            m_runtimeContext.Startup();
            m_embeddedApp->OnStartup(*m_embeddedHost);
            // Project-default UI theme (game-ui.md P3): the same manifest reference the
            // player honors at startup, applied to the embedded runtime's game UI so
            // Simulate/Game-tab/previews style like the shipped game.
            if (m_project && m_resources && m_embeddedApp->UI() != nullptr)
            {
                const Guid themeId = m_project->Settings().defaultUiThemeId;
                if (!themeId.IsNil())
                {
                    if (auto themeProxy = m_resources->Bind<draconic::ui::UITheme>(themeId))
                    {
                        m_embeddedApp->UI()->SetDefaultTheme(themeProxy.Get());
                    }
                }
            }

            // Per-subsystem editor plugins register here (page factories, creators, ...), and
            // the exe injects the engine interfaces the app drives (SetSceneRenderer). They
            // receive the EMBEDDED host: every page's Ctx() resolves to the runtime context.
            if (m_config.registerEditors) { m_config.registerEditors(*this, *m_embeddedHost, *m_uiHost); }

            // Cook service + the real Assets panel, once the project AND the exe-registered
            // builders both exist.
            if (m_project)
            {
                m_cookService.Initialize(*m_project, m_builders);
                // Pages request re-cooks after saving builder-backed assets (materials etc.).
                m_context.OnCookRequested = [this](bool rebuild) { m_cookService.RequestCook(rebuild); };
                // Background jobs (export) read the source DB structure and pack cooked FILES
                // from their worker - DB mutations and new cooks must hold off while one runs,
                // exactly like during a cook. The cook service folds this into MutationLocked.
                m_cookService.ExternalMutationLock = [this]() { return m_jobService.IsBusy(); };
                m_assetsView = MakeRef<AssetsView>(DefaultAllocator(), m_context, m_cookService, &m_jobService);
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

        /// Opening a page over uncooked content queues a scoped cook: every resource id
        /// that was requested during the page's resolve but has no product (the
        /// ResourceManager caches a null-product handle for each miss, and product guid ==
        /// source guid, so those ids are the exact missing-dependency roots), plus the
        /// page's own asset when its cook is missing/failed. Fully cooked pages request
        /// nothing - no worker spin-up on every open (six restored pages = six no-op cooks
        /// otherwise).
        void CookMissingForPage(draconic::content::Instance& instance)
        {
            if (m_project.Get() == nullptr) { return; }
            Array<Guid> unresolved;
            if (m_resources) { m_resources->CollectUnresolved(unresolved); }
            // Only ids with a live SOURCE instance can cook - a stale ref to a deleted
            // asset stays unresolved forever and must not re-request a cook on every open.
            Array<Guid> roots;
            for (const Guid& id : unresolved)
            {
                if (m_project->SourceDb().GetInstance(id) != nullptr) { roots.PushBack(id); }
            }
            const CookBadge badge = m_cookService.BadgeFor(instance);
            if (badge == CookBadge::Missing || badge == CookBadge::Failed)
            {
                roots.PushBack(instance.Id());
            }
            if (roots.IsEmpty()) { return; }
            m_cookService.RequestCookFor(Move(roots), false);
        }

        /// Open (or focus) the singleton Game tab (play-in-editor: the player behavior
        /// in-process; the page's own toolbar runs Play/Stop). Created through the scene
        /// editor plugin's factory seam - editor.app never links scene modules.
        // `newInstance` = false: the normal Play (focus the existing tab or open one on the primary
        // instance). true: "Play New Instance" - an ADDITIONAL Game tab driving its own GameInstance
        // (multi-instance PIE, game-instance.md §11 step 5).
        void OpenGamePage(bool newInstance = false)
        {
            if (!newInstance && m_gamePage != nullptr)
            {
                for (const PagePanel& entry : m_pagePanels)
                {
                    if (entry.page == m_gamePage)
                    {
                        m_shell.Docks()->ActivatePanel(entry.panel);
                        m_context.SetActivePage(m_gamePage);
                        return;
                    }
                }
            }
            if (!m_context.GamePageFactory)
            {
                m_context.Notify(ed::NoticeKind::Info, u8"No game page registered in this build.");
                return;
            }
            UniquePtr<draconic::editor::EditorPage> page = m_context.GamePageFactory(newInstance);
            if (!page) { return; }
            // All pages in this app are UIEditorPages (:ui_page contract), the Game page too.
            UIEditorPage* uiPage = static_cast<UIEditorPage*>(m_context.AdoptPage(Move(page)));
            if (uiPage == nullptr) { return; }
            if (!newInstance) { m_gamePage = uiPage; }   // only the primary tab is the focus target

            tk::DockablePanel* panel = m_shell.AddPagePanel(uiPage->Title(), uiPage->ContentView());
            // Unique persistence id per tab (extras get a counter so a docking restore can't collide).
            if (newInstance)
            {
                const String id = Format(u8"game-page-{}", ++m_gamePageCounter);
                panel->SetPersistenceId(id.AsView());
            }
            else { panel->SetPersistenceId(u8"game-page"); }
            panel->OnCloseRequested.Add([this, uiPage](tk::DockablePanel*) {
                m_uiHost->Context().MutationQueueRef().QueueAction(
                    Function<void()>{ [this, uiPage]() { ClosePage(uiPage); } });
            });
            m_pagePanels.PushBack(PagePanel{ uiPage, panel });
        }

        /// Open (or focus) a page for `instance` and dock its content as a center tab.
        UIEditorPage* OpenInstancePage(draconic::content::Instance& instance)
        {
            const usize before = m_context.OpenPages().Size();
            draconic::editor::EditorPage* page = m_context.OpenPage(instance);
            if (page == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Warning, u8"No editor registered for this asset type.");
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
            CookMissingForPage(instance);   // uncooked dependencies cook without a manual step
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
            if (page == m_gamePage) { m_gamePage = nullptr; }
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
            // Drive the embedded runtime's frame lanes FIRST: per-scene fixed stepping runs
            // in BeginFrame, physics interpolation in Update - pages then read fresh state.
            // (EndFrame closes in OnRenderWindow after the scene bracket.) Mirrors the middle
            // of ApplicationHost::Tick; the embedded app's OnUpdate itself (game script) is
            // driven by the Game page's play bracket, not here.
            if (m_embeddedApp)
            {
                const f32 scaled = dt * m_runtimeContext.TimeScale();
                m_runtimeContext.BeginFrame(dt);
                // Fixed lane: per-scene fixed stepping already ran in BeginFrame, but the APP-level
                // OnFixedUpdate (networking - each GameInstance's DriveNetwork) is otherwise never
                // driven in the editor. Accumulate + step it at the runtime's fixed rate, mirroring
                // ApplicationHost::Tick. No-op until an instance goes online, so always safe.
                const rt::ApplicationSettings settings = m_embeddedApp->Settings();
                m_embeddedFixedStepper.step = settings.fixedTimeStep;
                m_embeddedFixedStepper.maxSteps = settings.maxFixedStepsPerFrame;
                const u32 fixedSteps = m_embeddedFixedStepper.Advance(scaled);
                for (u32 i = 0; i < fixedSteps; ++i)
                {
                    m_embeddedApp->OnFixedUpdate(*m_embeddedHost, settings.fixedTimeStep);
                }
                m_runtimeContext.Update(scaled);
                m_runtimeContext.PostUpdate(scaled);
                // Tick EVERY game instance's script ONCE per frame (game-instance.md §11 step 5) -
                // moved here from the Game page so N game tabs don't tick every instance N times.
                m_embeddedApp->OnUpdate(*m_embeddedHost, dt);
            }

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

            // Deferred embedded-exit: safe here - no script dispatch is on the stack.
            if (m_stopGameRequested)
            {
                m_stopGameRequested = false;
                if (m_context.StopGameRun) { m_context.StopGameRun(); }
            }
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
                // Post-compose overlays (the Game tab's screen-tier UI onto its viewport).
                for (const PagePanel& entry : m_pagePanels) { entry.page->OnAfterSceneRender(host, frame); }
                if (m_embeddedApp) { m_runtimeContext.EndFrame(); }
            }
            if (m_uiHost) { m_uiHost->RenderWindow(frame); }
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            m_cookService.Shutdown();   // joins any in-flight cook before the DBs go away
            // Release page resources while the device and windows are still alive. Pages
            // destroy their scenes in the RUNTIME context, so it must outlive them.
            for (const PagePanel& entry : m_pagePanels) { entry.page->OnClose(); }
            SaveLayout();
            if (m_embeddedApp)
            {
                m_embeddedApp->OnShutdown(*m_embeddedHost);
                m_runtimeContext.Shutdown();
            }
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
            if (m_cookService.MutationLocked())
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
                m_context.Notify(ed::NoticeKind::Error, u8"Create failed (no project open?).");
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
                else
                {
                    m_context.Notify(ed::NoticeKind::Error,
                                     u8"Save FAILED (see console) - staying open.");
                }
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
        // Recursive walk feeding the export pre-transcode (main thread; scene/prefab typed
        // instances only - the stager filters).
        void CollectSceneStreams(draconic::content::Group& group)
        {
            for (draconic::content::Instance* instance : group.Instances())
            {
                Array<byte> bytes;
                if (m_context.SceneStreamStager(*instance, bytes))
                {
                    m_exportSceneStreams.InsertOrAssign(instance->Id(), Move(bytes));
                }
            }
            for (draconic::content::Group* child : group.Groups()) { CollectSceneStreams(*child); }
        }

        void LoadEditorSettings()
        {
            ed::RegisterEditorSettingsTypes();
            (void)ed::LoadEditorSettingsFromUserData(m_editorSettings);   // NotFound on first run is fine
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

        // Absolute form of `path`, resolved against the CWD. The project directory can be relative,
        // but the folder-reveal and clean logs need an absolute path (a relative one would reveal a
        // nonexistent path). Already-absolute paths pass through unchanged; if the CWD can't be read,
        // PathJoin degrades to the original relative path.
        [[nodiscard]] static String Absolutize(StringView path)
        {
            if (PathIsAbsolute(path)) { return String(path); }
            return PathJoin(GetCurrentDirectory().AsView(), path);
        }

        // Does this export run include a preset that prunes to reachable? (m_exportPresets must be
        // loaded first.) Decides whether the main-thread reachability pre-scan is worth running.
        [[nodiscard]] bool AnyPresetPrunes(bool all, StringView presetName) const
        {
            if (all)
            {
                for (const ed::ExportPreset& p : m_exportPresets.presets)
                {
                    if (p.pruneToReachable) { return true; }
                }
                return false;
            }
            const ed::ExportPreset* p = m_exportPresets.Find(presetName);
            return p != nullptr && p->pruneToReachable;
        }

        void SubmitExportJob(String presetName, bool all)
        {
            draconic::editor::EditorProject* project = m_project.Get();
            draconic::editor::BuilderRegistry* builders = &m_builders;

            // MAIN-THREAD pre-pass: transcode scene/prefab TEXT sources to the binary wire
            // (the stager needs the SceneSubsystem). One export at a time (IsBusy-guarded),
            // so the member map stays valid for the job's lifetime.
            m_exportSceneStreams.Clear();
            if (m_context.SceneStreamStager)
            {
                CollectSceneStreams(*m_project->SourceDb().RootGroup());
            }
            const HashMap<Guid, Array<byte>>* sceneStreams = &m_exportSceneStreams;

            // Load the presets on the MAIN thread (was in the job): the pre-scan below needs to know
            // whether pruning is requested, and the job then reuses this copy instead of re-reading.
            m_exportPresets.presets.Clear();
            {
                draconic::vfs::NativeFileSystem projectFs(project->Directory());
                if (!ed::LoadExportPresets(projectFs, m_exportPresets).IsOk())
                {
                    ed::DefaultExportPresets(m_exportPresets);
                }
            }

            // MAIN-THREAD reachability pre-scan (docs/design/export-reachability.md): pruning needs
            // the scene->asset edges, which means LOADING scenes - unsafe off the main thread. So when
            // a preset in this run prunes and the scene editor supplied a scanner, expand the reachable
            // closure NOW and hand the guid set to the background job (which then only cooks + packs
            // that set - pure I/O). Without a scanner the job falls back to pack-everything (safe).
            m_exportReachableRoots.Clear();
            m_exportReachableValid = false;
            if (AnyPresetPrunes(all, presetName.AsView()) && m_context.SceneRefScanner)
            {
                EditorApplication* self = this;
                const ed::SceneReferenceScanner adapter =
                    [self](draconic::content::Instance& inst, draconic::content::ContentDatabase& db,
                           ed::SceneReferences& refs) {
                        self->m_context.SceneRefScanner(inst, db, refs.resources, refs.prefabs);
                    };
                const Array<ed::ExportRoot> seeds = ed::CollectExportRoots(*project);
                m_exportReachableRoots = ed::ExpandReachableRoots(*project, seeds, adapter);
                m_exportReachableValid = true;
            }
            const Array<Guid>* reachableRoots = m_exportReachableValid ? &m_exportReachableRoots : nullptr;
            const ed::ExportPresetSet* presetsPtr = &m_exportPresets;

            const String toolDir = GetExecutableDirectory();
            const String templatesRoot = TemplatesRoot();   // resolve on the main thread (reads settings)
            const String outRoot = Absolutize(PathJoin(m_project->Directory(), u8"Dist").AsView());
            const String title(all ? StringView(u8"Export All") : StringView(u8"Export"));

            m_jobService.Submit(title.AsView(),
                [project, builders, toolDir, templatesRoot, presetName, all, outRoot, sceneStreams,
                 reachableRoots, presetsPtr](ed::JobContext& ctx) -> Status
                {
                    draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
                    draconic::vfs::NativeFileSystem rootFs(templatesRoot.AsView());   // imported templates
                    ed::TemplateRegistry registry;
                    registry.Refresh(templatesRoot.AsView(), &rootFs, toolDir.AsView(), &toolFs);
                    const ed::ExportPresetSet& presets = *presetsPtr;   // loaded on the main thread
                    const ed::ExportProgress onProgress = [&ctx](StringView step, f32 frac)
                    { ctx.SetStep(step); ctx.SetFraction(frac); };

                    if (all)
                    {
                        const Span<const ed::ExportPreset> span(presets.presets.Data(), presets.presets.Size());
                        return ed::ExportAll(*project, span, registry, *builders, outRoot.AsView(),
                                             /*rebuild*/ false, onProgress, /*cook*/ false, sceneStreams,
                                             /*scanner*/ nullptr, reachableRoots);
                    }
                    const ed::ExportPreset* preset = presets.Find(presetName.AsView());
                    if (preset == nullptr) { return Status{ ErrorCode::NotFound }; }
                    ed::ExportResult result;
                    return ed::ExportOne(*project, *preset, registry, *builders, outRoot.AsView(),
                                         /*rebuild*/ false, &result, onProgress, /*cook*/ false, sceneStreams,
                                         /*scanner*/ nullptr, reachableRoots);
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
                            DRACONIC_LOG_DEBUG(u8"Editor", u8"Open Folder clicked -> reveal '{}'", outRoot.AsView());
                            if (m_host != nullptr && m_host->Shell() != nullptr
                                && m_host->Shell()->Dialogs() != nullptr)
                            {
                                m_host->Shell()->Dialogs()->OpenPath(outRoot.AsView());
                            }
                            else
                            {
                                DRACONIC_LOG_WARNING(u8"Editor", u8"Open Folder: no shell dialog service available");
                            }
                        };
                        (void)m_toastHost->Show(Move(request));
                    }
                });
        }

        // === Templates + presets UI (File > Export... / File > Manage Templates...) ===
        //
        // Two management surfaces over the shared export driver: a templates manager (list + import /
        // create / remove installed bundles, with an engine-version match note) and an export-presets
        // panel (list + add / edit / duplicate / delete + Export / Export All). The presets panel drives
        // a preset-editor form; both defer every view-destroying action (dialog swap, list refresh)
        // through the UI mutation queue, and copy any async native-dialog paths before touching the UI.
        // The non-UI logic lives in ExportPresetsController + the template registry/helpers (tested).

        // Build a template registry on the MAIN thread: the host template (this editor's own Bin dir)
        // plus imported/created bundles under the configured templates root. Self-contained after
        // Refresh (copies manifests + dir paths), so it outlives the temporary filesystems here.
        void BuildTemplateRegistryMainThread(ed::TemplateRegistry& out)
        {
            const String toolDir = GetExecutableDirectory();
            const String templatesRoot = TemplatesRoot();
            draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
            draconic::vfs::NativeFileSystem rootFs(templatesRoot.AsView());
            out.Refresh(templatesRoot.AsView(), &rootFs, toolDir.AsView(), &toolFs);
        }

        // A labeled form row: fixed-width label + `field` (grows to fill). Returns the row so a caller
        // can append trailing controls (e.g. a "Browse..." button beside a text field).
        ui::FlexLayout* AddFormRow(ui::FlexLayout& column, StringView label, ui::View* field)
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 8;
            {
                auto text = MakeRef<ui::Label>(DefaultAllocator(), label);
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = ui::SizeSpec::Fixed(ui::Unit::Px(120));
                lp->AlignSelf = ui::Align::Center;
                row->AddView(text.Get(), lp);
            }
            if (field != nullptr)
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                lp->AlignSelf = ui::Align::Center;
                row->AddView(field, lp);
            }
            ui::FlexLayout* raw = row.Get();
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            column.AddView(row.Get(), lp);
            return raw;
        }

        // Swap the currently-open management dialog for a freshly-built one: close `current` and run
        // `open`, all on the UI mutation queue (never destroy/rebuild views mid-event-dispatch).
        void QueueReplaceDialog(ui::Dialog* current, Function<void()> open)
        {
            m_uiHost->Context().MutationQueueRef().QueueAction(Function<void()>{
                [current, open = Move(open)]()
            {
                if (current != nullptr) { current->Close(); }
                open();
            } });
        }

        void ReopenExportPresetsPanel(ui::Dialog* current)
        {
            QueueReplaceDialog(current, Function<void()>{ [this]() { OpenExportPresetsPanel(); } });
        }
        void ReopenTemplatesManager(ui::Dialog* current)
        {
            QueueReplaceDialog(current, Function<void()>{ [this]() { OpenTemplatesManager(); } });
        }

        // Persist the in-memory preset set to the project's export_presets.xml.
        void SavePresetsController()
        {
            if (!m_project) { return; }
            draconic::vfs::NativeFileSystem projectFs(m_project->Directory());
            if (!m_presetsController.Save(*projectFs.AsWritable()).IsOk())
            {
                m_context.Notify(ed::NoticeKind::Error, u8"Saving export presets FAILED (see console).");
            }
        }

        // Join / split the additionalFiles list <-> the ";"-separated text of the editor's Extra Files
        // field (an EditText holds no array, so the form marshals through a single string).
        [[nodiscard]] static String JoinSemicolons(const Array<String>& items)
        {
            String out;
            for (usize i = 0; i < items.Size(); ++i)
            {
                if (i > 0) { out += u8";"; }
                out += items[i].AsView();
            }
            return out;
        }
        static void SplitSemicolons(StringView text, Array<String>& out)
        {
            const auto isSpace = [](utf8char c) { return c == utf8char(' ') || c == utf8char('\t'); };
            usize start = 0;
            for (usize i = 0; i <= text.Size(); ++i)
            {
                if (i != text.Size() && text[i] != utf8char(';')) { continue; }
                usize s = start, e = i;
                while (s < e && isSpace(text[s])) { ++s; }
                while (e > s && isSpace(text[e - 1])) { --e; }
                if (e > s) { out.PushBack(String(text.SubStr(s, e - s))); }
                start = i + 1;
            }
        }

        // Native multi-select "open file" -> append the chosen absolute paths to the Extra Files field.
        // `target` is held by RefPtr so the field survives even if the form closes before the async
        // dialog resolves (SetText on a detached view is harmless); no view hierarchy is rebuilt.
        void PickAdditionalFiles(RefPtr<ui::EditText> target)
        {
            if (m_host == nullptr || m_host->Shell() == nullptr || m_host->Shell()->Dialogs() == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Error, u8"File dialogs are unavailable.");
                return;
            }
            m_host->Shell()->Dialogs()->ShowOpenFile(
                draconic::shell::DialogResultCallback{ [target](Span<const String> paths)
                {
                    if (paths.Size() == 0) { return; }   // cancelled
                    String text(target->Text());
                    for (usize i = 0; i < paths.Size(); ++i)
                    {
                        if (!text.IsEmpty() && text[text.Size() - 1] != utf8char(';')) { text += u8";"; }
                        text += paths[i].AsView();
                    }
                    target->SetText(text.AsView());
                } }, {}, {}, /*allowMultiple*/ true);
        }

        // Import a template bundle (folder with a template.xml) into the templates root, then rebuild
        // the manager. Async: the picked path is copied into ImportTemplate before any UI mutation.
        void ImportTemplateThenRefresh(ui::Dialog* current)
        {
            if (m_host == nullptr || m_host->Shell() == nullptr || m_host->Shell()->Dialogs() == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Error, u8"File dialogs are unavailable.");
                return;
            }
            m_host->Shell()->Dialogs()->ShowOpenFolder(
                draconic::shell::DialogResultCallback{ [this, current](Span<const String> paths)
                {
                    if (paths.Size() == 0) { return; }   // cancelled - leave the manager open
                    const String root = TemplatesRoot();
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
                    ReopenTemplatesManager(current);
                } });
        }

        // Create a template bundle from a "Bin/<Config>/<Platform>-<Compiler>" build dir (packaging the
        // player + its runtime-libs), installing it into the templates root, then rebuild the manager.
        void CreateTemplateThenRefresh(ui::Dialog* current)
        {
            if (m_host == nullptr || m_host->Shell() == nullptr || m_host->Shell()->Dialogs() == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Error, u8"File dialogs are unavailable.");
                return;
            }
            m_host->Shell()->Dialogs()->ShowOpenFolder(
                draconic::shell::DialogResultCallback{ [this, current](Span<const String> paths)
                {
                    if (paths.Size() == 0) { return; }   // cancelled
                    const String root = TemplatesRoot();
                    String id, dir;
                    if (ed::CreateTemplate(paths[0].AsView(), root.AsView(), ed::TemplateOutput::Install,
                                           &id, &dir).IsOk())
                    {
                        String msg(u8"Created template '"); msg += id; msg += u8"'.";
                        m_context.Notify(ed::NoticeKind::Success, msg.AsView());
                    }
                    else
                    {
                        m_context.Notify(ed::NoticeKind::Error,
                            u8"Create failed - pick a Bin/<Config> build dir containing RaptorPlayer.");
                    }
                    ReopenTemplatesManager(current);
                } });
        }

        // Templates manager: list every registry template (imported + the synthesized host), each with
        // its platform/config/engine-version and a soft "(!) engine mismatch" note; Import / Create /
        // Remove (non-host only) mutate the templates root and rebuild this dialog.
        void OpenTemplatesManager()
        {
            if (!m_uiHost) { return; }

            ed::TemplateRegistry registry;
            BuildTemplateRegistryMainThread(registry);

            auto dialog = MakeRef<ui::Dialog>(DefaultAllocator(), StringView(u8"Manage Export Templates"));
            dialog->MinWidth.SetValue(560.0f);
            dialog->MaxWidth.SetValue(780.0f);
            dialog->MinHeight.SetValue(240.0f);
            dialog->MaxHeight.SetValue(560.0f);
            ui::Dialog* raw = dialog.Get();

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            auto header = MakeRef<ui::Label>(DefaultAllocator(),
                StringView(u8"Installed export templates (the host build is always available):"));
            column->AddView(header.Get());

            for (usize i = 0; i < registry.Count(); ++i)
            {
                const ed::ExportTemplate* t = registry.At(i);
                if (t == nullptr) { continue; }
                String text(t->name.AsView());
                text += u8"  ["; text += t->platform.AsView();
                text += u8"/";  text += t->EffectiveConfig(); text += u8"]";
                if (!t->engineVersion.IsEmpty()) { text += u8"  v"; text += t->engineVersion.AsView(); }
                if (t->isHost) { text += u8"  (host)"; }
                if (!ed::TemplateEngineMatches(*t)) { text += u8"  (!) engine mismatch"; }

                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 8;
                {
                    auto label = MakeRef<ui::Label>(DefaultAllocator(), text.AsView());
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Grow = 1.0f;
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(label.Get(), lp);
                }
                if (!t->isHost)   // the host template is synthesized, never on disk => not removable
                {
                    const String id(t->id.AsView());
                    auto remove = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Remove"));
                    remove->OnClick.Add([this, raw, id](ui::ButtonBase*)
                    {
                        const String root = TemplatesRoot();
                        if (ed::RemoveTemplate(root.AsView(), id.AsView()).IsOk())
                        {
                            String msg(u8"Removed template '"); msg += id; msg += u8"'.";
                            m_context.Notify(ed::NoticeKind::Success, msg.AsView());
                        }
                        else { m_context.Notify(ed::NoticeKind::Error, u8"Remove failed (see console)."); }
                        ReopenTemplatesManager(raw);
                    });
                    row->AddView(remove.Get());
                }
                column->AddView(row.Get());
            }

            dialog->SetContent(column.Get());

            ui::Button* import = dialog->AddButton(u8"Import...", ui::DialogResult::None);
            import->OnClick.Add([this, raw](ui::ButtonBase*) { ImportTemplateThenRefresh(raw); });
            ui::Button* create = dialog->AddButton(u8"Create...", ui::DialogResult::None);
            create->OnClick.Add([this, raw](ui::ButtonBase*) { CreateTemplateThenRefresh(raw); });
            dialog->AddButton(u8"Close", ui::DialogResult::Cancel);
            dialog->Show(&m_uiHost->Context());
        }

        // Export presets panel: (re)load the project's export_presets.xml into the controller, list each
        // preset with per-row Export / Edit / Duplicate / Delete, plus Add / Export All / Manage
        // Templates in the footer. Edit/Add open the preset-editor form (swapping this dialog).
        void OpenExportPresetsPanel()
        {
            if (!m_project) { m_context.Notify(ed::NoticeKind::Info, u8"Open a project first."); return; }
            if (!m_uiHost) { return; }

            {
                draconic::vfs::NativeFileSystem projectFs(m_project->Directory());
                m_presetsController.Load(projectFs);   // reflects edits persisted by the editor form
            }

            auto dialog = MakeRef<ui::Dialog>(DefaultAllocator(), StringView(u8"Export"));
            dialog->MinWidth.SetValue(600.0f);
            dialog->MaxWidth.SetValue(820.0f);
            dialog->MinHeight.SetValue(220.0f);
            dialog->MaxHeight.SetValue(560.0f);
            ui::Dialog* raw = dialog.Get();

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            auto info = MakeRef<ui::Label>(DefaultAllocator(),
                StringView(u8"Export presets (output directory: <project>/Dist):"));
            column->AddView(info.Get());

            for (usize i = 0; i < m_presetsController.Count(); ++i)
            {
                const ed::ExportPreset& p = m_presetsController.At(i);
                String text(p.name.AsView());
                text += u8"  [";
                text += p.platform.IsEmpty() ? StringView(u8"?") : p.platform.AsView();
                text += u8"/";
                text += p.config.IsEmpty() ? StringView(u8"Release") : p.config.AsView();
                text += u8"]";

                auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
                row->Direction = ui::Orientation::Horizontal;
                row->Spacing = 6;
                {
                    auto label = MakeRef<ui::Label>(DefaultAllocator(), text.AsView());
                    auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                    lp->Grow = 1.0f;
                    lp->AlignSelf = ui::Align::Center;
                    row->AddView(label.Get(), lp);
                }
                const String name(p.name.AsView());
                const usize index = i;
                {
                    auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Export"));
                    b->OnClick.Add([this, raw, name](ui::ButtonBase*)
                    {
                        RunExport(name.AsView(), false);
                        raw->Close(ui::DialogResult::OK);
                    });
                    row->AddView(b.Get());
                }
                {
                    auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Edit"));
                    b->OnClick.Add([this, raw, index](ui::ButtonBase*)
                    {
                        ed::ExportPreset current = m_presetsController.At(index);
                        QueueReplaceDialog(raw, Function<void()>{ [this, current, index]()
                            { OpenPresetEditor(current, static_cast<isize>(index)); } });
                    });
                    row->AddView(b.Get());
                }
                {
                    auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Duplicate"));
                    b->OnClick.Add([this, raw, index](ui::ButtonBase*)
                    {
                        m_presetsController.Duplicate(index);
                        SavePresetsController();
                        ReopenExportPresetsPanel(raw);
                    });
                    row->AddView(b.Get());
                }
                {
                    auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Delete"));
                    b->OnClick.Add([this, raw, index](ui::ButtonBase*)
                    {
                        m_presetsController.Remove(index);
                        SavePresetsController();
                        ReopenExportPresetsPanel(raw);
                    });
                    row->AddView(b.Get());
                }
                column->AddView(row.Get());
            }

            dialog->SetContent(column.Get());

            ui::Button* add = dialog->AddButton(u8"Add...", ui::DialogResult::None);
            add->OnClick.Add([this, raw](ui::ButtonBase*)
            {
                ed::ExportPreset fresh;
                fresh.name = String(u8"New Preset");
                fresh.platform = String(GetHostPlatformName());
                QueueReplaceDialog(raw, Function<void()>{ [this, fresh]() { OpenPresetEditor(fresh, -1); } });
            });
            ui::Button* exportAll = dialog->AddButton(u8"Export All", ui::DialogResult::None);
            exportAll->OnClick.Add([this, raw](ui::ButtonBase*)
            {
                RunExport(StringView{}, true);
                raw->Close(ui::DialogResult::OK);
            });
            ui::Button* templates = dialog->AddButton(u8"Manage Templates...", ui::DialogResult::None);
            templates->OnClick.Add([this, raw](ui::ButtonBase*)
            {
                QueueReplaceDialog(raw, Function<void()>{ [this]() { OpenTemplatesManager(); } });
            });
            dialog->AddButton(u8"Close", ui::DialogResult::Cancel);
            dialog->Show(&m_uiHost->Context());
        }

        // Preset-editor form: name, a template dropdown (registry.All(): sets templateId + derives
        // platform/config) OR explicit platform/config when "(resolve by ...)" is chosen, player name,
        // output subdir, additionalFiles (native multi-select picker) and the stageSymbols /
        // pruneToReachable toggles. Save writes through the controller (Add when `editIndex` < 0, else
        // Update), persists, and returns to the presets panel; Cancel just returns.
        void OpenPresetEditor(ed::ExportPreset initial, isize editIndex)
        {
            if (!m_uiHost) { return; }

            ed::TemplateRegistry registry;
            BuildTemplateRegistryMainThread(registry);

            auto dialog = MakeRef<ui::Dialog>(DefaultAllocator(),
                StringView(editIndex < 0 ? u8"Add Export Preset" : u8"Edit Export Preset"));
            dialog->MinWidth.SetValue(600.0f);
            dialog->MaxWidth.SetValue(820.0f);
            dialog->MinHeight.SetValue(340.0f);
            dialog->MaxHeight.SetValue(640.0f);
            ui::Dialog* raw = dialog.Get();

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            auto nameEdit = MakeRef<ui::EditText>(DefaultAllocator());
            nameEdit->SetText(initial.name.AsView());
            AddFormRow(*column, u8"Name", nameEdit.Get());

            // Template dropdown: index 0 = resolve by platform/config; each later item maps to a
            // concrete templateId (+ its platform/config), captured into the parallel arrays below.
            auto templateCombo = MakeRef<ui::ComboBox>(DefaultAllocator());
            templateCombo->AddItem(u8"(resolve by platform + config below)");
            Array<String> comboIds, comboPlatforms, comboConfigs;
            comboIds.PushBack(String{}); comboPlatforms.PushBack(String{}); comboConfigs.PushBack(String{});
            i32 selectedCombo = 0;
            for (usize i = 0; i < registry.Count(); ++i)
            {
                const ed::ExportTemplate* t = registry.At(i);
                if (t == nullptr) { continue; }
                String item(t->name.AsView());
                item += u8" ["; item += t->platform.AsView();
                item += u8"/"; item += t->EffectiveConfig(); item += u8"]";
                if (t->isHost) { item += u8" (host)"; }
                const i32 idx = templateCombo->AddItem(item.AsView());
                comboIds.PushBack(String(t->id.AsView()));
                comboPlatforms.PushBack(String(t->platform.AsView()));
                comboConfigs.PushBack(String(t->EffectiveConfig()));
                if (!initial.templateId.IsEmpty() && initial.templateId.AsView() == t->id.AsView())
                {
                    selectedCombo = idx;
                }
            }
            templateCombo->SetSelectedIndex(selectedCombo);
            AddFormRow(*column, u8"Template", templateCombo.Get());

            auto platformEdit = MakeRef<ui::EditText>(DefaultAllocator());
            platformEdit->SetText(initial.platform.AsView());
            platformEdit->SetPlaceholder(GetHostPlatformName());
            AddFormRow(*column, u8"Platform", platformEdit.Get());

            auto configEdit = MakeRef<ui::EditText>(DefaultAllocator());
            configEdit->SetText(initial.config.AsView());
            configEdit->SetPlaceholder(u8"Release");
            AddFormRow(*column, u8"Config", configEdit.Get());

            auto playerEdit = MakeRef<ui::EditText>(DefaultAllocator());
            playerEdit->SetText(initial.playerName.AsView());
            playerEdit->SetPlaceholder(u8"(template default)");
            AddFormRow(*column, u8"Player name", playerEdit.Get());

            auto subdirEdit = MakeRef<ui::EditText>(DefaultAllocator());
            subdirEdit->SetText(initial.outputSubdir.AsView());
            subdirEdit->SetPlaceholder(u8"(sanitized name)");
            AddFormRow(*column, u8"Output subdir", subdirEdit.Get());

            auto filesEdit = MakeRef<ui::EditText>(DefaultAllocator());
            filesEdit->SetText(JoinSemicolons(initial.additionalFiles).AsView());
            filesEdit->SetPlaceholder(u8"icon.ico;config.xml");
            ui::FlexLayout* filesRow = AddFormRow(*column, u8"Extra files", filesEdit.Get());
            {
                RefPtr<ui::EditText> filesRef = filesEdit;
                auto browse = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Add Files..."));
                browse->OnClick.Add([this, filesRef](ui::ButtonBase*) { PickAdditionalFiles(filesRef); });
                filesRow->AddView(browse.Get());
            }

            auto symbolsCheck = MakeRef<ui::CheckBox>(DefaultAllocator(),
                StringView(u8"Stage debug symbols into the dist"), initial.stageSymbols);
            column->AddView(symbolsCheck.Get());
            auto pruneCheck = MakeRef<ui::CheckBox>(DefaultAllocator(),
                StringView(u8"Prune to reachable content only"), initial.pruneToReachable);
            column->AddView(pruneCheck.Get());

            dialog->SetContent(column.Get());

            ui::EditText* nameRaw = nameEdit.Get();
            ui::ComboBox* comboRaw = templateCombo.Get();
            ui::EditText* platformRaw = platformEdit.Get();
            ui::EditText* configRaw = configEdit.Get();
            ui::EditText* playerRaw = playerEdit.Get();
            ui::EditText* subdirRaw = subdirEdit.Get();
            ui::EditText* filesRaw = filesEdit.Get();
            ui::CheckBox* symbolsRaw = symbolsCheck.Get();
            ui::CheckBox* pruneRaw = pruneCheck.Get();

            ui::Button* save = dialog->AddButton(u8"Save", ui::DialogResult::None);
            save->OnClick.Add([this, raw, editIndex, nameRaw, comboRaw, platformRaw, configRaw, playerRaw,
                               subdirRaw, filesRaw, symbolsRaw, pruneRaw,
                               comboIds, comboPlatforms, comboConfigs](ui::ButtonBase*)
            {
                ed::ExportPreset result;
                result.name = String(nameRaw->Text());
                const i32 sel = comboRaw->SelectedIndex();
                if (sel > 0 && static_cast<usize>(sel) < comboIds.Size())
                {
                    result.templateId = comboIds[static_cast<usize>(sel)];
                    result.platform = comboPlatforms[static_cast<usize>(sel)];
                    result.config = comboConfigs[static_cast<usize>(sel)];
                }
                else
                {
                    result.platform = String(platformRaw->Text());
                    result.config = String(configRaw->Text());
                }
                result.playerName = String(playerRaw->Text());
                result.outputSubdir = String(subdirRaw->Text());
                result.stageSymbols = symbolsRaw->IsChecked.Value();
                result.pruneToReachable = pruneRaw->IsChecked.Value();
                SplitSemicolons(filesRaw->Text(), result.additionalFiles);

                if (editIndex < 0) { m_presetsController.Add(result); }
                else { m_presetsController.Update(static_cast<usize>(editIndex), result); }
                SavePresetsController();
                ReopenExportPresetsPanel(raw);
            });
            ui::Button* cancel = dialog->AddButton(u8"Cancel", ui::DialogResult::None);
            cancel->OnClick.Add([this, raw](ui::ButtonBase*) { ReopenExportPresetsPanel(raw); });
            dialog->Show(&m_uiHost->Context());
        }

        // Save As: write the page's CURRENT content to a NEW asset beside the original and
        // rebind the page to it. The original keeps its on-disk state - the escape hatch when
        // an asset changed under a dirty page (apply-to-prefab) and both versions matter.
        void SaveActivePageAs()
        {
            ed::EditorPage* page = m_context.ActivePage();
            if (page == nullptr || m_project.Get() == nullptr || m_uiHost.Get() == nullptr) { return; }
            draconic::content::Instance* original =
                m_project->SourceDb().GetInstance(page->InstanceId());
            if (original == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Warning, u8"This page has no source asset to copy.");
                return;
            }
            const TypeInfo* type = GlobalTypeRegistry().FindByName(
                reinterpret_cast<const char*>(String(original->TypeNamespace()).CStr()),
                reinterpret_cast<const char*>(String(original->TypeName()).CStr()));
            if (type == nullptr)
            {
                m_context.Notify(ed::NoticeKind::Error, u8"Save As: unknown asset type.");
                return;
            }

            draconic::content::Group* group = &original->OwningGroup();
            String suggested(original->Name());
            suggested += u8" Copy";
            while (group->GetInstance(suggested.AsView()) != nullptr) { suggested += u8" Copy"; }

            String prompt(u8"New name (created next to '");
            prompt += original->Name();
            prompt += u8"'):";
            RefPtr<draconic::ui::Dialog> dialog =
                MakeRef<draconic::ui::Dialog>(DefaultAllocator(), StringView(u8"Save As"));
            auto column = MakeRef<draconic::ui::FlexLayout>(DefaultAllocator());
            column->Direction = draconic::ui::Orientation::Vertical;
            column->Spacing = 6.0f;
            RefPtr<draconic::ui::Label> label =
                MakeRef<draconic::ui::Label>(DefaultAllocator(), prompt.AsView());
            label->WordWrap.SetValue(true);
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                column->AddView(label.Get(), lp);
            }
            auto nameEdit = MakeRef<draconic::ui::EditText>(DefaultAllocator());
            nameEdit->SetText(suggested.AsView());
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                column->AddView(nameEdit.Get(), lp);
            }
            // Inline validation line: empty until a rejected attempt; the dialog stays up.
            auto errorLabel = MakeRef<draconic::ui::Label>(DefaultAllocator());
            errorLabel->WordWrap.SetValue(true);
            errorLabel->TextColor.SetValue(Color{ 0.90f, 0.35f, 0.35f, 1.0f });
            {
                auto lp = MakeRef<draconic::ui::FlexLayoutParams>(DefaultAllocator());
                lp->Width = draconic::ui::SizeSpec::Match();
                column->AddView(errorLabel.Get(), lp);
            }
            dialog->SetContent(column.Get());

            draconic::ui::Dialog* rawDialog = dialog.Get();
            draconic::ui::EditText* rawEdit = nameEdit.Get();
            draconic::ui::Label* rawError = errorLabel.Get();
            const Guid pageId = page->InstanceId();
            draconic::ui::Button* save =
                dialog->AddButton(u8"Save", draconic::ui::DialogResult::None);
            save->OnClick.Add([this, pageId, group, type, rawDialog, rawEdit, rawError](draconic::ui::ButtonBase*) {
                const StringView newName = rawEdit->Text();
                if (newName.IsEmpty())
                {
                    rawError->SetText(u8"NOT saved: enter a name.");
                    return;   // dialog stays up for the retry
                }
                if (group->GetInstance(newName) != nullptr)
                {
                    rawError->SetText(u8"NOT saved: that name already exists in the group.");
                    return;   // dialog stays up for the retry
                }
                // Re-resolve the page: the dialog is modal-ish but pages can close under it.
                ed::EditorPage* target = nullptr;
                for (const UniquePtr<ed::EditorPage>& open : m_context.OpenPages())
                {
                    if (open->InstanceId() == pageId) { target = open.Get(); break; }
                }
                if (target == nullptr)
                {
                    rawDialog->Close(draconic::ui::DialogResult::Cancel);
                    return;
                }
                draconic::content::Instance* fresh = group->CreateInstance(newName, *type);
                if (fresh == nullptr)
                {
                    m_context.Notify(ed::NoticeKind::Error,
                                     u8"NOT saved: could not create the new asset.");
                    return;
                }
                target->OnSavedAs(*fresh);
                if (target->Save().IsOk())
                {
                    String message(u8"Saved as '");
                    message += fresh->Name();
                    message += u8"'.";
                    m_context.Notify(ed::NoticeKind::Success, message.AsView());
                }
                else
                {
                    m_context.Notify(ed::NoticeKind::Error, u8"Save As FAILED (see Console).");
                }
                rawDialog->Close(draconic::ui::DialogResult::OK);
            });
            dialog->AddButton(u8"Cancel", draconic::ui::DialogResult::Cancel);
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
                    m_context.Notify(ed::NoticeKind::Error,
                                     u8"Save FAILED (see console) - page stays open.");
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
                    // Instance-less pages (the Game tab) don't persist in the page set - a
                    // nil guid would just fail the restore lookup.
                    if (entry.page->InstanceId().IsNil()) { continue; }
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
                file->AddItem(u8"Save As...", [this]() { SaveActivePageAs(); });
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
                file->AddItem(u8"Preferences...", [this]() {
                    auto dialog = MakeRef<EditorPreferencesDialog>(DefaultAllocator(), m_context, m_editorSettings);
                    dialog->Show(&m_uiHost->Context());
                });
                file->AddSeparator();
                file->AddItem(u8"Export...", [this]() { OpenExportPresetsPanel(); });
                file->AddItem(u8"Manage Templates...", [this]() { OpenTemplatesManager(); });
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

            if (draconic::ui::ContextMenu* game = bar->AddMenu(u8"Game"))
            {
                game->AddItem(u8"Play", [this]() { OpenGamePage(false); });
                game->AddItem(u8"Play New Instance", [this]() { OpenGamePage(true); });
            }
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
        draconic::render::ISceneRenderer* m_sceneRenderer = nullptr;
        // The embedded runtime (v3): gameplay subsystems + ALL scene hosting live here.
        rt::Context m_runtimeContext;
        UniquePtr<rt::EmbeddedApplicationHost> m_embeddedHost;
        rt::FixedStepper m_embeddedFixedStepper;   // drives the embedded app's OnFixedUpdate (net) in-editor
        UniquePtr<rt::DefaultApplication> m_embeddedApp;
        bool m_stopGameRequested = false;   // borrowed (exe injects)

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
        PendingExport m_pendingExport;
        ed::ExportPresetsController m_presetsController;   // backs the Export presets panel + editor form
        HashMap<Guid, Array<byte>> m_exportSceneStreams;   // export pre-transcoded scene wires
        ed::ExportPresetSet m_exportPresets;               // main-thread-loaded presets for the running job
        Array<Guid> m_exportReachableRoots;                // main-thread pre-scan result (reachable closure roots)
        bool m_exportReachableValid = false;               // true when the pre-scan ran (else no pruning this run)
        UIEditorPage* m_gamePage = nullptr;                // the PRIMARY game tab (focus target); extras untracked
        u32 m_gamePageCounter = 0;                         // unique persistence id for "Play New Instance" tabs
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
