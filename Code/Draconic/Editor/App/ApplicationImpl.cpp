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

module draconic.editor.app;

import draconic.core;
import draconic.shell;
import draconic.graphics;
import draconic.fonts;
import draconic.fonts.ttf;
import draconic.fonts.df.baker; // DFFonts (MSDF baker registration) for the DF font path
import draconic.runtime;
import draconic.runtime.client;
import draconic.runtime.defaultapp; // the embedded game application (v3)
import draconic.ui.resource;        // UITheme (the manifest's default game-UI theme)
import draconic.ui.subsystem;       // UISubsystem (SetDefaultTheme)
import draconic.input.subsystem;    // InputSubsystem (the embedded runtime's scene-input policy)
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

namespace draconic::editor::app
{
    // Editor text rendering: false = the classic per-size raster ramp; true = MSDF
    // distance-field atlases (one 48px bake per family). NOTE the DF path is NOT ready for
    // transparent UI use yet - it needs per-size scaled font views in the font service and
    // DF handling in VGContext::DrawPositionedGlyphs; until then text renders at the baked
    // 48px and shaped runs sample the raw MSDF texture (rainbow glyphs).
    constexpr bool kUseDistanceFieldFonts = false;

    draconic::editor::EditorProject* EditorApplication::Project() const noexcept
    {
        return m_project.Get();
    }

    draconic::editor::EditorCookService& EditorApplication::CookService() noexcept
    {
        return m_cookService;
    }

    void
    EditorApplication::AddResourceFactory(UniquePtr<draconic::resource::IResourceFactory> factory)
    {
        if (!factory)
        {
            return;
        }
        if (m_resources)
        {
            m_resources->AddFactory(factory.Get());
        }
        m_resourceFactories.PushBack(Move(factory));
    }

    draconic::resource::ResourceManager* EditorApplication::Resources() const noexcept
    {
        return m_resources.Get();
    }

    runtime::DefaultApplication* EditorApplication::EmbeddedApplication() const noexcept
    {
        return m_embeddedApp.Get();
    }

    void EditorApplication::Configure(runtime::IApplicationHost& host)
    {
        if (m_config.configureEngine)
        {
            m_config.configureEngine(host);
        }
    }

    void EditorApplication::SetSceneRenderer(draconic::render::ISceneRenderer* renderer) noexcept
    {
        m_sceneRenderer = renderer;
    }

    void EditorApplication::OnStartup(runtime::IApplicationHost& host)
    {
        m_host = &host;
        graphics::RenderWindow* mainRw = host.MainRenderWindow();
        if (mainRw == nullptr)
        {
            return;
        }

        // Fonts (CPU rasterization/baking; no device needed).
        m_fontService = MakeUnique<fonts::TrueTypeFontService>(DefaultAllocator());
        if (kUseDistanceFieldFonts)
        {
            // MSDF path: ONE atlas per family, baked at 48px, sampled crisp at every size
            // the styles request (GetFont's closest-size fallback lands on it). The VG
            // renderer switches to the distance-field pipeline per glyph run automatically.
            fonts::DFFonts::Initialize();
            fonts::FontLoadOptions options = fonts::FontLoadOptions::DistanceField();
            options.pixelHeight = 48.0f;
            options.firstCodepoint = 32;
            options.lastCodepoint = 255; // the ExtendedLatin range the raster path bakes
            options.atlasWidth = 1024;
            options.atlasHeight = 1024;
            if (!m_config.fontPath.IsEmpty())
            {
                (void)m_fontService->LoadFont(u8"Roboto", m_config.fontPath.AsView(), options);
            }
            if (!m_config.monoFontPath.IsEmpty())
            {
                (void)m_fontService->LoadFont(u8"Mono", m_config.monoFontPath.AsView(), options);
            }
        }
        else
        {
            if (!m_config.fontPath.IsEmpty())
            {
                fonts::FontLoadOptions options = fonts::FontLoadOptions::ExtendedLatin();
                // A full ramp so styles can pick small (property fields), regular, and
                // heading sizes without falling back to a mismatched rasterization.
                const f32 sizes[] = {10.0f, 11.0f, 12.0f, 13.0f, 14.0f,
                                     16.0f, 18.0f, 20.0f, 24.0f, 32.0f};
                for (f32 size : sizes)
                {
                    options.pixelHeight = size;
                    (void)m_fontService->LoadFont(u8"Roboto", m_config.fontPath.AsView(),
                                                  options);
                }
            }
            if (!m_config.monoFontPath.IsEmpty())
            {
                // Fixed-pitch family for CodeEditView (script/shader/XML pages). Smaller ramp:
                // code text only needs the field-to-heading range.
                fonts::FontLoadOptions options = fonts::FontLoadOptions::ExtendedLatin();
                const f32 monoSizes[] = {10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 16.0f};
                for (f32 size : monoSizes)
                {
                    options.pixelHeight = size;
                    (void)m_fontService->LoadFont(u8"Mono", m_config.monoFontPath.AsView(),
                                                  options);
                }
            }
        }

        LoadEditorSettings(); // per-user prefs (templates root, ...); absent on first run
        EditorIcons::Get().Initialize(); // shared SVG drawables (toolbar + asset types)
        m_uiHost = MakeUnique<ui::runtime::UIHost>(DefaultAllocator(), *host.Graphics(),
                                                   *host.Shell(), *m_fontService);
        m_dockHost = MakeUnique<ui::application::RuntimeDockableWindowHost>(DefaultAllocator(),
                                                                            host, *m_uiHost);

        // Theme: register the toolkit extension BEFORE creating the stylesheet (extensions
        // only apply to themes built afterward), then the editor defaults to dark.
        draconic::ui::ThemeRegistry::RegisterExtension(&m_toolkitTheme);
        // Editor theme: the warm "Graphite & Orange" palette on the rounded theme (soft corners
        // everywhere) - a crafted, less-bland alternative to the stock flat/square cool-grey dark.
        m_styleSheet =
            draconic::ui::RoundedDarkTheme::Create(draconic::ui::ThemePalette::GraphiteOrange());
        // Editor-specific overrides on top of the stock theme: property-grid fields read
        // better noticeably smaller and tighter than the theme's 14px/6x4 control chrome
        // (a full inspector column of them is the densest text in the editor).
        m_styleSheet->ForClass(u8"property-field")
            .Set(draconic::ui::StyleProperty::FontSize, 12.0f)
            .Set(draconic::ui::StyleProperty::Padding, draconic::ui::Thickness{5, 2});
        m_uiHost->Context().SetStyleSheet(m_styleSheet);

        // Exit goes through the dirty check: the shell consults this before honoring the
        // main window's close button / OS quit; File>Exit routes through the same helper.
        host.Shell()->OnMainWindowCloseRequested = [this]() { return ConfirmExitAllowed(); };

        m_shell.Build(m_context, m_dockHost.Get(), mainRw->Window().Width(),
                      mainRw->Window().Height());
        // The ACTIVE page follows dock-tab activation, not just OpenPage/ClosePage - with
        // side-by-side tab groups, Save was hitting whichever page opened last, not the tab
        // the user selected. Non-page panels (Console, Assets) leave the active page alone.
        m_shell.Docks()->OnPanelActivated.Add(
            draconic::ui::Event<void(ui::toolkit::DockablePanel*)>::Handler{
                [this](ui::toolkit::DockablePanel* panel)
                {
                    if (panel == nullptr)
                    {
                        return;
                    }
                    for (const PagePanel& entry : m_pagePanels)
                    {
                        if (entry.panel == panel)
                        {
                            m_context.SetActivePage(entry.page);
                            return;
                        }
                    }
                }});
        m_uiHost->AttachWindow(mainRw, RefPtr<draconic::ui::RootView>(m_shell.Root()));

        // Toast overlay on the main window root (input passes through outside the cards);
        // EditorContext::Notify routes here, and also mirrors to the status bar.
        m_toastHost = MakeRef<ui::toolkit::ToastHost>(DefaultAllocator());
        m_shell.Root()->AddView(m_toastHost.Get());
        m_context.OnNotice = [this](editor::NoticeKind kind, StringView message)
        {
            ShowToast(kind, message);
            m_context.SetStatus(message);
        };

        OpenProject();
        if (m_project)
        {
            // Per-user pinned assets (browser + picker surface them first).
            (void)LoadFavorites(m_context, m_project->EditorStateRoot().AsView());
            m_context.OnFavoritesChanged = [this]()
            {
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
            for (const auto& factory : m_resourceFactories)
            {
                m_resources->AddFactory(factory.Get());
            }
            m_context.SetResources(m_resources.Get());
        }
        m_embeddedHost = MakeUnique<runtime::EmbeddedApplicationHost>(DefaultAllocator(), host,
                                                                      m_runtimeContext);
        m_embeddedHost->SetExitHandler(Function<void(int)>{
            [this](int code)
            {
                // "Exit" from embedded game code = stop the play session. DEFERRED to
                // after the page-update loop: the request usually fires from inside the
                // game script's update(), and Stop tears the script down.
                DRACONIC_LOG_INFO(u8"Editor", u8"embedded app requested exit({})", code);
                m_stopGameRequested = true;
            }});
        m_embeddedApp = MakeUnique<runtime::DefaultApplication>(DefaultAllocator());
        if (!m_config.fontPath.IsEmpty())
        {
            m_embeddedApp->SetUIFontPath(m_config.fontPath.AsView());
        }
        if (m_resources)
        {
            m_embeddedApp->SetResourceManager(m_resources.Get());
        }
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
        if (m_config.registerEditors)
        {
            m_config.registerEditors(*this, *m_embeddedHost, *m_uiHost);
        }

        // Cook service + the real Assets panel, once the project AND the exe-registered
        // builders both exist.
        if (m_project)
        {
            m_cookService.Initialize(*m_project, m_builders);
            // Pages request re-cooks after saving builder-backed assets (materials etc.).
            m_context.OnCookRequested = [this](bool rebuild)
            { m_cookService.RequestCook(rebuild); };
            // Background jobs (export) read the source DB structure and pack cooked FILES
            // from their worker - DB mutations and new cooks must hold off while one runs,
            // exactly like during a cook. The cook service folds this into MutationLocked.
            m_cookService.ExternalMutationLock = [this]() { return m_jobService.IsBusy(); };
            m_assetsView =
                MakeRef<AssetsView>(DefaultAllocator(), m_context, m_cookService, &m_jobService);
            AssetsView* assets = m_assetsView.Get();
            m_assetsView->OnOpenInstance = [this](draconic::content::Instance& instance)
            { (void)OpenInstancePage(instance); };
            m_assetsView->OnCreate =
                [this](const draconic::editor::EditorContext::AssetCreator& creator,
                       draconic::content::Group* group) { CreateAndOpen(creator, group); };
            // Delete-while-open policy: close-then-delete. Called from a mutation-queue
            // action (never mid-event-dispatch), so synchronous panel + page teardown is
            // safe here - the same pair of steps the tab close button triggers.
            m_assetsView->OnCloseInstancePage = [this](const Guid& id)
            {
                for (usize i = 0; i < m_pagePanels.Size(); ++i)
                {
                    if (m_pagePanels[i].page->InstanceId() == id)
                    {
                        ui::toolkit::DockablePanel* panel = m_pagePanels[i].panel;
                        UIEditorPage* page = m_pagePanels[i].page;
                        m_shell.Docks()->ClosePanel(panel);
                        ClosePage(page);
                        return;
                    }
                }
            };
            m_cookService.OnCookFinished = [this, assets]()
            {
                assets->Rebuild();
                // Result toast: failures are sticky (Console has the log); silent when the
                // cook was a no-op (the watcher fires those constantly).
                const usize failed = m_cookService.LastFailedCount();
                const usize cooked = m_cookService.LastCookedCount();
                if (failed > 0)
                {
                    ShowToast(editor::NoticeKind::Error,
                              Format(u8"Cook: {} failed, {} cooked (see Console).", failed, cooked)
                                  .AsView());
                }
                else if (cooked > 0)
                {
                    ShowToast(editor::NoticeKind::Success,
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
                    if (draconic::content::Instance* instance =
                            m_project->SourceDb().GetInstance(id))
                    {
                        UIEditorPage* page = OpenInstancePage(*instance);
                        if (page != nullptr && id == activePage)
                        {
                            toActivate = page;
                        }
                    }
                }
                if (toActivate != nullptr)
                {
                    m_context.SetActivePage(toActivate);
                }
            }
            else
            {
                // No saved page set (first launch): fall back to the default scene -
                // guid first (authoritative), path mirror for guid-less manifests.
                draconic::content::Instance* instance = nullptr;
                if (!m_project->Settings().defaultSceneId.IsNil())
                {
                    instance =
                        m_project->SourceDb().GetInstance(m_project->Settings().defaultSceneId);
                }
                if (instance == nullptr && !m_project->Settings().defaultScene.IsEmpty())
                {
                    instance = m_project->SourceDb().GetInstance(
                        m_project->Settings().defaultScene.AsView());
                }
                if (instance != nullptr)
                {
                    (void)OpenInstancePage(*instance);
                }
            }
            (void)m_shell.RestoreLayout(m_project->EditorStateRoot().AsView());
        }
    }

    void EditorApplication::CookMissingForPage(draconic::content::Instance& instance)
    {
        if (m_project.Get() == nullptr)
        {
            return;
        }
        Array<Guid> unresolved;
        if (m_resources)
        {
            m_resources->CollectUnresolved(unresolved);
        }
        // Only ids with a live SOURCE instance can cook - a stale ref to a deleted
        // asset stays unresolved forever and must not re-request a cook on every open.
        Array<Guid> roots;
        for (const Guid& id : unresolved)
        {
            if (m_project->SourceDb().GetInstance(id) != nullptr)
            {
                roots.PushBack(id);
            }
        }
        const CookBadge badge = m_cookService.BadgeFor(instance);
        if (badge == CookBadge::Missing || badge == CookBadge::Failed)
        {
            roots.PushBack(instance.Id());
        }
        if (roots.IsEmpty())
        {
            return;
        }
        m_cookService.RequestCookFor(Move(roots), false);
    }

    void EditorApplication::OpenGamePage(bool newInstance)
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
            m_context.Notify(editor::NoticeKind::Info, u8"No game page registered in this build.");
            return;
        }
        UniquePtr<draconic::editor::EditorPage> page = m_context.GamePageFactory(newInstance);
        if (!page)
        {
            return;
        }
        // All pages in this app are UIEditorPages (:ui_page contract), the Game page too.
        UIEditorPage* uiPage = static_cast<UIEditorPage*>(m_context.AdoptPage(Move(page)));
        if (uiPage == nullptr)
        {
            return;
        }
        if (!newInstance)
        {
            m_gamePage = uiPage;
        } // only the primary tab is the focus target

        ui::toolkit::DockablePanel* panel =
            m_shell.AddPagePanel(uiPage->Title(), uiPage->ContentView());
        // Unique persistence id per tab (extras get a counter so a docking restore can't collide).
        if (newInstance)
        {
            const String id = Format(u8"game-page-{}", ++m_gamePageCounter);
            panel->SetPersistenceId(id.AsView());
        }
        else
        {
            panel->SetPersistenceId(u8"game-page");
        }
        panel->OnCloseRequested.Add(
            [this, uiPage](ui::toolkit::DockablePanel*)
            {
                m_uiHost->Context().MutationQueueRef().QueueAction(
                    Function<void()>{[this, uiPage]() { ClosePage(uiPage); }});
            });
        m_pagePanels.PushBack(PagePanel{uiPage, panel});
    }

    UIEditorPage* EditorApplication::OpenInstancePage(draconic::content::Instance& instance)
    {
        const usize before = m_context.OpenPages().Size();
        draconic::editor::EditorPage* page = m_context.OpenPage(instance);
        if (page == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Warning,
                             u8"No editor registered for this asset type.");
            return nullptr;
        }
        // All factories in this app produce UIEditorPages (:ui_page contract).
        UIEditorPage* uiPage = static_cast<UIEditorPage*>(page);
        if (m_context.OpenPages().Size() == before)
        {
            // Focused an existing page - select its tab.
            for (const PagePanel& entry : m_pagePanels)
            {
                if (entry.page == uiPage)
                {
                    m_shell.Docks()->ActivatePanel(entry.panel);
                    break;
                }
            }
            return uiPage;
        }

        ui::toolkit::DockablePanel* panel =
            m_shell.AddPagePanel(uiPage->Title(), uiPage->ContentView());
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
        panel->OnCloseRequested.Add(
            [this, uiPage](ui::toolkit::DockablePanel*)
            {
                m_uiHost->Context().MutationQueueRef().QueueAction(
                    Function<void()>{[this, uiPage]() { ClosePage(uiPage); }});
            });
        // Dirty pages don't close silently: veto the gesture and prompt Save / Discard /
        // Cancel. The dialog's buttons invoke OnCloseRequested DIRECTLY (bypassing this
        // veto), which runs the normal dock + page teardown.
        panel->OnCloseInterceptor = [this, uiPage](ui::toolkit::DockablePanel* p) -> bool
        {
            if (!uiPage->IsDirty())
            {
                return true;
            }
            ShowDirtyCloseDialog(uiPage, p);
            return false;
        };
        m_pagePanels.PushBack(PagePanel{uiPage, panel});
        CookMissingForPage(instance); // uncooked dependencies cook without a manual step
        return uiPage;
    }

    void EditorApplication::ShowToast(editor::NoticeKind kind, StringView message)
    {
        if (m_toastHost.Get() == nullptr)
        {
            return;
        }
        ui::toolkit::ToastRequest request;
        request.message = String(message);
        switch (kind)
        {
        case editor::NoticeKind::Success:
            request.severity = ui::toolkit::ToastSeverity::Success;
            break;
        case editor::NoticeKind::Warning:
            request.severity = ui::toolkit::ToastSeverity::Warning;
            break;
        case editor::NoticeKind::Error:
            request.severity = ui::toolkit::ToastSeverity::Error;
            break;
        case editor::NoticeKind::Info:
        default:
            request.severity = ui::toolkit::ToastSeverity::Info;
            break;
        }
        request.durationSeconds = (kind == editor::NoticeKind::Error) ? 0.0f : 5.0f; // errors stick
        (void)m_toastHost->Show(Move(request));
    }

    void EditorApplication::SaveActivePage()
    {
        auto* page = m_context.ActivePage();
        if (page == nullptr)
        {
            return;
        }
        if (page->Save().IsOk())
        {
            String message(u8"Saved '");
            message += page->Title();
            message += u8"'.";
            m_context.Notify(editor::NoticeKind::Success, message.AsView());
        }
        else
        {
            m_context.Notify(editor::NoticeKind::Error, u8"Save FAILED (see Console).");
        }
    }

    void EditorApplication::ClosePage(UIEditorPage* page)
    {
        if (page == m_gamePage)
        {
            m_gamePage = nullptr;
        }
        for (usize i = 0; i < m_pagePanels.Size(); ++i)
        {
            if (m_pagePanels[i].page == page)
            {
                if (page->IsDirty())
                {
                    m_context.SetStatus(
                        u8"Closed page had unsaved changes."); // save-prompt = later phase
                }
                page->OnClose(); // release GPU/scene resources while device + window live
                m_pagePanels.RemoveAt(i);
                m_context.ClosePage(page); // destroys the page
                return;
            }
        }
    }

    void EditorApplication::OnUpdate(runtime::IApplicationHost& host, f32 dt)
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
            const runtime::ApplicationSettings settings = m_embeddedApp->Settings();
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
            if (m_config.autoRebuildSeconds > 0.0f && !m_autoRebuilt &&
                m_elapsed >= m_config.autoRebuildSeconds)
            {
                m_autoRebuilt = true;
                m_cookService.RequestCook(true);
            }
        }
        // Headless-debug hook: RAPTOR_TEST_OPEN=<guid> opens that instance's page ~2s in
        // and opens it AGAIN ~4s in (the focus-existing branch) - reproduces the asset
        // browser's double-click paths in unattended (ASAN/gdb) runs.
        if (const char* testOpen = std::getenv("RAPTOR_TEST_OPEN");
            testOpen != nullptr && m_project)
        {
            m_testOpenElapsed += dt;
            const bool first = m_testOpenStage == 0 && m_testOpenElapsed >= 2.0f;
            const bool second = m_testOpenStage == 1 && m_testOpenElapsed >= 4.0f;
            if (first || second)
            {
                ++m_testOpenStage;
                Guid id;
                if (Guid::TryParse(StringView(reinterpret_cast<const utf8char*>(testOpen)), id))
                {
                    if (draconic::content::Instance* instance =
                            m_project->SourceDb().GetInstance(id))
                    {
                        (void)OpenInstancePage(*instance);
                    }
                }
            }
        }

        // Headless-debug hook: RAPTOR_TEST_REIMPORT="<group>;<file>" deletes the named
        // source group ~2s in and reimports <file> ~4s in (the watcher recook follows) -
        // scripts the delete->reimport crash repro for unattended ASAN runs.
        if (const char* reimport = std::getenv("RAPTOR_TEST_REIMPORT");
            reimport != nullptr && m_project)
        {
            m_testOpenElapsed += dt; // shared timer with RAPTOR_TEST_OPEN (use one hook per run)
            const StringView spec(reinterpret_cast<const utf8char*>(reimport));
            usize semi = spec.Size();
            for (usize i = 0; i < spec.Size(); ++i)
            {
                if (spec.Data()[i] == u8';')
                {
                    semi = i;
                    break;
                }
            }
            if (semi < spec.Size())
            {
                if (m_testOpenStage == 0 && m_testOpenElapsed >= 2.0f)
                {
                    ++m_testOpenStage;
                    const String groupName(spec.SubStr(0, semi));
                    if (draconic::content::Group* group =
                            m_project->SourceDb().RootGroup()->GetGroup(groupName.AsView()))
                    {
                        m_cookService.RunWhenIdle(
                            Function<void()>{[this, group]()
                                             {
                                                 (void)m_project->SourceDb().DeleteGroup(*group);
                                                 m_context.SetStatus(u8"[test] deleted group");
                                                 // Mirror DeleteGroupNow: the assets tree holds raw Group*
                                                 // rows - EVERY source-DB group mutation must Rebuild before
                                                 // the next layout binds stale pointers.
                                                 if (m_assetsView)
                                                 {
                                                     m_assetsView->Rebuild();
                                                 }
                                             }});
                    }
                }
                else if (m_testOpenStage == 1 && m_testOpenElapsed >= 4.0f)
                {
                    ++m_testOpenStage;
                    const String file(spec.SubStr(semi + 1, spec.Size() - semi - 1));
                    m_context.SetStatus(u8"[test] reimporting");
                    if (m_assetsView)
                    {
                        m_assetsView->ImportFile(file.AsView());
                    }
                }
            }
        }

        DrainLog();
        SyncPageTitles();
        // Background-cook progress -> status bar (log lines reach the Console via the
        // logger); a finished cook refreshes the Assets badges through OnCookFinished.
        m_cookService.Update(
            Function<void(StringView)>{[this](StringView line) { m_context.SetStatus(line); }});

        // Background jobs: pump, drain job logs, and once an export's pre-cook has finished, submit
        // the export's pack/stage job. Show the running job's step + percent in the status bar.
        m_jobService.Update(
            Function<void(StringView)>{[this](StringView line) { m_context.SetStatus(line); }});
        if (m_pendingExport.active && m_pendingExport.waitingCook && !m_cookService.IsCooking())
        {
            m_pendingExport.waitingCook = false;
            SubmitExportJob(m_pendingExport.presetName, m_pendingExport.all);
            m_pendingExport.active = false; // the job owns it now
        }
        if (m_jobService.IsBusy())
        {
            const editor::EditorJobService::ProgressView p = m_jobService.Progress();
            if (p.active)
            {
                String s(p.title.AsView());
                if (!p.step.IsEmpty())
                {
                    s += u8": ";
                    s += p.step;
                }
                s += u8" (";
                AppendCountTo(s, static_cast<usize>(p.fraction * 100.0f + 0.5f));
                s += u8"%)";
                m_context.SetStatus(s.AsView());
            }
        }

        if (m_assetsView)
        {
            m_assetsView->Refresh();
        }
        if (m_resources)
        {
            m_resources->CollectGarbage();
        } // release hot-reloaded-away products

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
        if (m_uiHost)
        {
            m_uiHost->Update(dt);
        }
        if (m_toastHost)
        {
            m_toastHost->Update(dt);
        }
        if (m_dockHost)
        {
            m_dockHost->Tick();
        } // drag-follow for floating OS windows

        // Page hooks AFTER the UI laid out (viewport rects are current for input gating).
        for (const PagePanel& entry : m_pagePanels)
        {
            entry.page->OnUpdate(host, dt);
        }

        // Deferred embedded-exit: safe here - no script dispatch is on the stack.
        if (m_stopGameRequested)
        {
            m_stopGameRequested = false;
            if (m_context.StopGameRun)
            {
                m_context.StopGameRun();
            }
        }
    }

    void EditorApplication::OnRenderWindow(runtime::IApplicationHost& host,
                                           graphics::FrameContext& frame)
    {
        // ALL pages' viewport content renders during the MAIN window's frame, inside ONE
        // scene-renderer bracket, BEFORE any UI draws (the Sedulous editor structure:
        // offscreen targets are window-agnostic, so floated panels' windows simply sample
        // the textures this pass produced). Secondary-window frames are UI-only.
        if (frame.valid && frame.window == host.MainRenderWindow())
        {
            // Through the ISceneRenderer INTERFACE (render.api) - editor.app never links the
            // renderer. Begin/EndRendering self-guard while the renderer isn't ready.
            if (m_sceneRenderer != nullptr)
            {
                m_sceneRenderer->BeginRendering(*frame.encoder, frame.frameIndex);
            }
            for (const PagePanel& entry : m_pagePanels)
            {
                entry.page->OnRenderWindow(host, frame);
            }
            if (m_sceneRenderer != nullptr)
            {
                m_sceneRenderer->EndRendering();
            }
            // Post-compose overlays (the Game tab's screen-tier UI onto its viewport).
            for (const PagePanel& entry : m_pagePanels)
            {
                entry.page->OnAfterSceneRender(host, frame);
            }
            if (m_embeddedApp)
            {
                m_runtimeContext.EndFrame();
            }
        }
        if (m_uiHost)
        {
            m_uiHost->RenderWindow(frame);
        }
    }

    void EditorApplication::OnShutdown(runtime::IApplicationHost&)
    {
        m_cookService.Shutdown(); // joins any in-flight cook before the DBs go away
        // Release page resources while the device and windows are still alive. Pages
        // destroy their scenes in the RUNTIME context, so it must outlive them.
        for (const PagePanel& entry : m_pagePanels)
        {
            entry.page->OnClose();
        }
        SaveLayout();
        if (m_embeddedApp)
        {
            m_embeddedApp->OnShutdown(*m_embeddedHost);
            m_runtimeContext.Shutdown();
        }
        EditorIcons::Get().Shutdown(); // release drawables deterministically
        if (kUseDistanceFieldFonts)
        {
            fonts::DFFonts::Shutdown(); // unregister the MSDF baker (mirror of OnStartup)
        }
    }

    void
    EditorApplication::CreateAndOpen(const draconic::editor::EditorContext::AssetCreator& creator,
                                     draconic::content::Group* group)
    {
        // Cook gate: the plan worker reads the DBs with their structure frozen -
        // creating instances mid-plan is a race. Queue and replay when idle.
        if (m_cookService.MutationLocked())
        {
            const draconic::editor::EditorContext::AssetCreator* entry = &creator;
            m_cookService.RunWhenIdle(
                Function<void()>{[this, entry, group]() { CreateAndOpen(*entry, group); }});
            m_context.Notify(editor::NoticeKind::Info,
                             u8"Create queued until the current cook finishes.");
            return;
        }
        draconic::content::Instance* instance = creator.create(m_context, group);
        if (instance == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Error, u8"Create failed (no project open?).");
            return;
        }
        if (creator.setsDefaultScene && m_project && m_project->Settings().defaultSceneId.IsNil() &&
            m_project->Settings().defaultScene.IsEmpty())
        {
            m_project->Settings().defaultSceneId = instance->Id();
            m_project->Settings().defaultScene = instance->Path();
            (void)m_project->SaveSettings();
        }
        // Surface the new row immediately (the File-menu path bypasses the assets view's own
        // rebuild) and cook it so builder-backed assets become pickable without a manual
        // Cook All (a no-op for builder-less scenes: nothing is dirty).
        if (m_assetsView)
        {
            m_assetsView->Rebuild();
        }
        if (m_builders.FindByTypeName(instance->TypeName()) != nullptr)
        {
            m_cookService.RequestCook(false);
        }
        (void)OpenInstancePage(*instance);
    }

    bool EditorApplication::ConfirmExitAllowed()
    {
        usize dirtyCount = 0;
        for (const PagePanel& entry : m_pagePanels)
        {
            if (entry.page->IsDirty())
            {
                ++dirtyCount;
            }
        }
        if (dirtyCount == 0)
        {
            return true;
        }

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
        saveAll->OnClick.Add(
            [this, rawDialog](draconic::ui::ButtonBase*)
            {
                bool allSaved = true;
                for (const PagePanel& entry : m_pagePanels)
                {
                    if (entry.page->IsDirty() && !entry.page->Save().IsOk())
                    {
                        allSaved = false;
                    }
                }
                if (allSaved)
                {
                    m_host->RequestExit();
                }
                else
                {
                    m_context.Notify(editor::NoticeKind::Error,
                                     u8"Save FAILED (see console) - staying open.");
                }
                rawDialog->Close(allSaved ? draconic::ui::DialogResult::OK
                                          : draconic::ui::DialogResult::Cancel);
            });
        draconic::ui::Button* discard =
            dialog->AddButton(u8"Exit Without Saving", draconic::ui::DialogResult::None);
        discard->OnClick.Add(
            [this, rawDialog](draconic::ui::ButtonBase*)
            {
                m_host->RequestExit();
                rawDialog->Close(draconic::ui::DialogResult::OK);
            });
        dialog->AddButton(u8"Cancel", draconic::ui::DialogResult::Cancel);
        dialog->Show(&m_uiHost->Context());
        return false;
    }

    void EditorApplication::AppendCountTo(String& out, usize value)
    {
        utf8char digits[20];
        usize n = 0;
        do
        {
            digits[n++] = static_cast<utf8char>('0' + (value % 10));
            value /= 10;
        } while (value != 0);
        while (n > 0)
        {
            out.PushBack(digits[--n]);
        }
    }

    void EditorApplication::RunExport(StringView presetName, bool all)
    {
        if (!m_project)
        {
            m_context.Notify(editor::NoticeKind::Info, u8"Open a project first.");
            return;
        }
        if (m_jobService.IsBusy() || m_pendingExport.active)
        {
            m_context.Notify(editor::NoticeKind::Info, u8"An export is already in progress.");
            return;
        }
        m_pendingExport =
            PendingExport{String(presetName), all, /*waitingCook*/ true, /*active*/ true};
        m_context.Notify(editor::NoticeKind::Info, u8"Cooking before export...");
        m_cookService.RequestCook(
            false); // safe background cook; OnUpdate fires the export job after it
    }

    void EditorApplication::CollectSceneStreams(draconic::content::Group& group)
    {
        for (draconic::content::Instance* instance : group.Instances())
        {
            Array<byte> bytes;
            if (m_context.SceneStreamStager(*instance, bytes))
            {
                m_exportSceneStreams.InsertOrAssign(instance->Id(), Move(bytes));
            }
        }
        for (draconic::content::Group* child : group.Groups())
        {
            CollectSceneStreams(*child);
        }
    }

    void EditorApplication::LoadEditorSettings()
    {
        editor::RegisterEditorSettingsTypes();
        (void)editor::LoadEditorSettingsFromUserData(
            m_editorSettings); // NotFound on first run is fine
    }

    String EditorApplication::TemplatesRoot() const
    {
        StringView overrideRoot;
        if (const editor::EditorExportSettings* s =
                m_editorSettings.Find<editor::EditorExportSettings>())
        {
            overrideRoot = s->templatesRoot.AsView();
        }
        return editor::ResolveTemplatesRoot(overrideRoot);
    }

    String EditorApplication::Absolutize(StringView path)
    {
        if (PathIsAbsolute(path))
        {
            return String(path);
        }
        return PathJoin(GetCurrentDirectory().AsView(), path);
    }

    bool EditorApplication::AnyPresetPrunes(bool all, StringView presetName) const
    {
        if (all)
        {
            for (const editor::ExportPreset& p : m_exportPresets.presets)
            {
                if (p.pruneToReachable)
                {
                    return true;
                }
            }
            return false;
        }
        const editor::ExportPreset* p = m_exportPresets.Find(presetName);
        return p != nullptr && p->pruneToReachable;
    }

    void EditorApplication::SubmitExportJob(String presetName, bool all)
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
            if (!editor::LoadExportPresets(projectFs, m_exportPresets).IsOk())
            {
                editor::DefaultExportPresets(m_exportPresets);
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
            const editor::SceneReferenceScanner adapter =
                [self](draconic::content::Instance& inst, draconic::content::ContentDatabase& db,
                       editor::SceneReferences& refs)
            { self->m_context.SceneRefScanner(inst, db, refs.resources, refs.prefabs); };
            const Array<editor::ExportRoot> seeds = editor::CollectExportRoots(*project);
            m_exportReachableRoots = editor::ExpandReachableRoots(*project, seeds, adapter);
            m_exportReachableValid = true;
        }
        const Array<Guid>* reachableRoots =
            m_exportReachableValid ? &m_exportReachableRoots : nullptr;
        const editor::ExportPresetSet* presetsPtr = &m_exportPresets;

        const String toolDir = GetExecutableDirectory();
        const String templatesRoot = TemplatesRoot(); // resolve on the main thread (reads settings)
        const String outRoot = Absolutize(PathJoin(m_project->Directory(), u8"Dist").AsView());
        const String title(all ? StringView(u8"Export All") : StringView(u8"Export"));

        m_jobService.Submit(
            title.AsView(),
            [project, builders, toolDir, templatesRoot, presetName, all, outRoot, sceneStreams,
             reachableRoots, presetsPtr](editor::JobContext& ctx) -> Status
            {
                draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
                draconic::vfs::NativeFileSystem rootFs(
                    templatesRoot.AsView()); // imported templates
                editor::TemplateRegistry registry;
                registry.Refresh(templatesRoot.AsView(), &rootFs, toolDir.AsView(), &toolFs);
                const editor::ExportPresetSet& presets = *presetsPtr; // loaded on the main thread
                const editor::ExportProgress onProgress = [&ctx](StringView step, f32 frac)
                {
                    ctx.SetStep(step);
                    ctx.SetFraction(frac);
                };

                if (all)
                {
                    const Span<const editor::ExportPreset> span(presets.presets.Data(),
                                                                presets.presets.Size());
                    return editor::ExportAll(*project, span, registry, *builders, outRoot.AsView(),
                                             /*rebuild*/ false, onProgress, /*cook*/ false,
                                             sceneStreams,
                                             /*scanner*/ nullptr, reachableRoots);
                }
                const editor::ExportPreset* preset = presets.Find(presetName.AsView());
                if (preset == nullptr)
                {
                    return Status{ErrorCode::NotFound};
                }
                editor::ExportResult result;
                return editor::ExportOne(*project, *preset, registry, *builders, outRoot.AsView(),
                                         /*rebuild*/ false, &result, onProgress, /*cook*/ false,
                                         sceneStreams,
                                         /*scanner*/ nullptr, reachableRoots);
            },
            [this, outRoot](Status s) // main thread
            {
                if (!s.IsOk())
                {
                    m_context.Notify(editor::NoticeKind::Error, u8"Export failed (see Console).");
                    return;
                }
                m_context.SetStatus(u8"Export complete.");
                // Sticky success toast with a button that reveals the Dist folder in the OS file
                // manager (the button click dismisses the toast, per ToastHost's onAction contract).
                if (m_toastHost.Get() != nullptr)
                {
                    ui::toolkit::ToastRequest request;
                    request.message = String(u8"Export complete.");
                    request.severity = ui::toolkit::ToastSeverity::Success;
                    request.durationSeconds = 0.0f; // sticky
                    request.actionLabel = String(u8"Open Folder");
                    request.onAction = [this, outRoot]()
                    {
                        DRACONIC_LOG_DEBUG(u8"Editor", u8"Open Folder clicked -> reveal '{}'",
                                           outRoot.AsView());
                        if (m_host != nullptr && m_host->Shell() != nullptr &&
                            m_host->Shell()->Dialogs() != nullptr)
                        {
                            m_host->Shell()->Dialogs()->OpenPath(outRoot.AsView());
                        }
                        else
                        {
                            DRACONIC_LOG_WARNING(
                                u8"Editor", u8"Open Folder: no shell dialog service available");
                        }
                    };
                    (void)m_toastHost->Show(Move(request));
                }
            });
    }

    void EditorApplication::BuildTemplateRegistryMainThread(editor::TemplateRegistry& out)
    {
        const String toolDir = GetExecutableDirectory();
        const String templatesRoot = TemplatesRoot();
        draconic::vfs::NativeFileSystem toolFs(toolDir.AsView());
        draconic::vfs::NativeFileSystem rootFs(templatesRoot.AsView());
        out.Refresh(templatesRoot.AsView(), &rootFs, toolDir.AsView(), &toolFs);
    }

    ui::FlexLayout* EditorApplication::AddFormRow(ui::FlexLayout& column, StringView label,
                                                  ui::View* field)
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

    void EditorApplication::QueueReplaceDialog(ui::Dialog* current, Function<void()> open)
    {
        m_uiHost->Context().MutationQueueRef().QueueAction(
            Function<void()>{[current, open = Move(open)]()
                             {
                                 if (current != nullptr)
                                 {
                                     current->Close();
                                 }
                                 open();
                             }});
    }

    void EditorApplication::ReopenExportPresetsPanel(ui::Dialog* current)
    {
        QueueReplaceDialog(current, Function<void()>{[this]() { OpenExportPresetsPanel(); }});
    }

    void EditorApplication::ReopenTemplatesManager(ui::Dialog* current)
    {
        QueueReplaceDialog(current, Function<void()>{[this]() { OpenTemplatesManager(); }});
    }

    void EditorApplication::SavePresetsController()
    {
        if (!m_project)
        {
            return;
        }
        draconic::vfs::NativeFileSystem projectFs(m_project->Directory());
        if (!m_presetsController.Save(*projectFs.AsWritable()).IsOk())
        {
            m_context.Notify(editor::NoticeKind::Error,
                             u8"Saving export presets FAILED (see console).");
        }
    }

    String EditorApplication::JoinSemicolons(const Array<String>& items)
    {
        String out;
        for (usize i = 0; i < items.Size(); ++i)
        {
            if (i > 0)
            {
                out += u8";";
            }
            out += items[i].AsView();
        }
        return out;
    }

    void EditorApplication::SplitSemicolons(StringView text, Array<String>& out)
    {
        const auto isSpace = [](utf8char c) { return c == utf8char(' ') || c == utf8char('\t'); };
        usize start = 0;
        for (usize i = 0; i <= text.Size(); ++i)
        {
            if (i != text.Size() && text[i] != utf8char(';'))
            {
                continue;
            }
            usize s = start, e = i;
            while (s < e && isSpace(text[s]))
            {
                ++s;
            }
            while (e > s && isSpace(text[e - 1]))
            {
                --e;
            }
            if (e > s)
            {
                out.PushBack(String(text.SubStr(s, e - s)));
            }
            start = i + 1;
        }
    }

    void EditorApplication::PickAdditionalFiles(RefPtr<ui::EditText> target)
    {
        if (m_host == nullptr || m_host->Shell() == nullptr ||
            m_host->Shell()->Dialogs() == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Error, u8"File dialogs are unavailable.");
            return;
        }
        m_host->Shell()->Dialogs()->ShowOpenFile(
            draconic::shell::DialogResultCallback{
                [target](Span<const String> paths)
                {
                    if (paths.Size() == 0)
                    {
                        return;
                    } // cancelled
                    String text(target->Text());
                    for (usize i = 0; i < paths.Size(); ++i)
                    {
                        if (!text.IsEmpty() && text[text.Size() - 1] != utf8char(';'))
                        {
                            text += u8";";
                        }
                        text += paths[i].AsView();
                    }
                    target->SetText(text.AsView());
                }},
            {}, {}, /*allowMultiple*/ true);
    }

    void EditorApplication::ImportTemplateThenRefresh(ui::Dialog* current)
    {
        if (m_host == nullptr || m_host->Shell() == nullptr ||
            m_host->Shell()->Dialogs() == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Error, u8"File dialogs are unavailable.");
            return;
        }
        m_host->Shell()->Dialogs()->ShowOpenFolder(draconic::shell::DialogResultCallback{
            [this, current](Span<const String> paths)
            {
                if (paths.Size() == 0)
                {
                    return;
                } // cancelled - leave the manager open
                const String root = TemplatesRoot();
                String id;
                if (editor::ImportTemplate(paths[0].AsView(), root.AsView(), &id).IsOk())
                {
                    String msg(u8"Imported template '");
                    msg += id;
                    msg += u8"'.";
                    m_context.Notify(editor::NoticeKind::Success, msg.AsView());
                }
                else
                {
                    m_context.Notify(editor::NoticeKind::Error,
                                     u8"Import failed - the folder has no valid template.xml.");
                }
                ReopenTemplatesManager(current);
            }});
    }

    void EditorApplication::CreateTemplateThenRefresh(ui::Dialog* current)
    {
        if (m_host == nullptr || m_host->Shell() == nullptr ||
            m_host->Shell()->Dialogs() == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Error, u8"File dialogs are unavailable.");
            return;
        }
        m_host->Shell()->Dialogs()->ShowOpenFolder(draconic::shell::DialogResultCallback{
            [this, current](Span<const String> paths)
            {
                if (paths.Size() == 0)
                {
                    return;
                } // cancelled
                const String root = TemplatesRoot();
                String id, dir;
                if (editor::CreateTemplate(paths[0].AsView(), root.AsView(),
                                           editor::TemplateOutput::Install, &id, &dir)
                        .IsOk())
                {
                    String msg(u8"Created template '");
                    msg += id;
                    msg += u8"'.";
                    m_context.Notify(editor::NoticeKind::Success, msg.AsView());
                }
                else
                {
                    m_context.Notify(editor::NoticeKind::Error,
                                     u8"Create failed - pick a Bin/<Config> build dir "
                                     u8"containing RaptorPlayer.");
                }
                ReopenTemplatesManager(current);
            }});
    }

    void EditorApplication::OpenTemplatesManager()
    {
        if (!m_uiHost)
        {
            return;
        }

        editor::TemplateRegistry registry;
        BuildTemplateRegistryMainThread(registry);

        auto dialog =
            MakeRef<ui::Dialog>(DefaultAllocator(), StringView(u8"Manage Export Templates"));
        dialog->MinWidth.SetValue(560.0f);
        dialog->MaxWidth.SetValue(780.0f);
        dialog->MinHeight.SetValue(240.0f);
        dialog->MaxHeight.SetValue(560.0f);
        ui::Dialog* raw = dialog.Get();

        auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        column->Direction = ui::Orientation::Vertical;
        column->Spacing = 6;

        auto header = MakeRef<ui::Label>(
            DefaultAllocator(),
            StringView(u8"Installed export templates (the host build is always available):"));
        column->AddView(header.Get());

        for (usize i = 0; i < registry.Count(); ++i)
        {
            const editor::ExportTemplate* t = registry.At(i);
            if (t == nullptr)
            {
                continue;
            }
            String text(t->name.AsView());
            text += u8"  [";
            text += t->platform.AsView();
            text += u8"/";
            text += t->EffectiveConfig();
            text += u8"]";
            if (!t->engineVersion.IsEmpty())
            {
                text += u8"  v";
                text += t->engineVersion.AsView();
            }
            if (t->isHost)
            {
                text += u8"  (host)";
            }
            if (!editor::TemplateEngineMatches(*t))
            {
                text += u8"  (!) engine mismatch";
            }

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
            if (!t->isHost) // the host template is synthesized, never on disk => not removable
            {
                const String id(t->id.AsView());
                auto remove = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Remove"));
                remove->OnClick.Add(
                    [this, raw, id](ui::ButtonBase*)
                    {
                        const String root = TemplatesRoot();
                        if (editor::RemoveTemplate(root.AsView(), id.AsView()).IsOk())
                        {
                            String msg(u8"Removed template '");
                            msg += id;
                            msg += u8"'.";
                            m_context.Notify(editor::NoticeKind::Success, msg.AsView());
                        }
                        else
                        {
                            m_context.Notify(editor::NoticeKind::Error,
                                             u8"Remove failed (see console).");
                        }
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

    void EditorApplication::OpenExportPresetsPanel()
    {
        if (!m_project)
        {
            m_context.Notify(editor::NoticeKind::Info, u8"Open a project first.");
            return;
        }
        if (!m_uiHost)
        {
            return;
        }

        {
            draconic::vfs::NativeFileSystem projectFs(m_project->Directory());
            m_presetsController.Load(projectFs); // reflects edits persisted by the editor form
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

        auto info = MakeRef<ui::Label>(
            DefaultAllocator(), StringView(u8"Export presets (output directory: <project>/Dist):"));
        column->AddView(info.Get());

        for (usize i = 0; i < m_presetsController.Count(); ++i)
        {
            const editor::ExportPreset& p = m_presetsController.At(i);
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
                b->OnClick.Add(
                    [this, raw, name](ui::ButtonBase*)
                    {
                        RunExport(name.AsView(), false);
                        raw->Close(ui::DialogResult::OK);
                    });
                row->AddView(b.Get());
            }
            {
                auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Edit"));
                b->OnClick.Add(
                    [this, raw, index](ui::ButtonBase*)
                    {
                        editor::ExportPreset current = m_presetsController.At(index);
                        QueueReplaceDialog(
                            raw,
                            Function<void()>{
                                [this, current, index]()
                                { OpenPresetEditor(current, static_cast<isize>(index)); }});
                    });
                row->AddView(b.Get());
            }
            {
                auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Duplicate"));
                b->OnClick.Add(
                    [this, raw, index](ui::ButtonBase*)
                    {
                        m_presetsController.Duplicate(index);
                        SavePresetsController();
                        ReopenExportPresetsPanel(raw);
                    });
                row->AddView(b.Get());
            }
            {
                auto b = MakeRef<ui::Button>(DefaultAllocator(), StringView(u8"Delete"));
                b->OnClick.Add(
                    [this, raw, index](ui::ButtonBase*)
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
        add->OnClick.Add(
            [this, raw](ui::ButtonBase*)
            {
                editor::ExportPreset fresh;
                fresh.name = String(u8"New Preset");
                fresh.platform = String(GetHostPlatformName());
                QueueReplaceDialog(
                    raw, Function<void()>{[this, fresh]() { OpenPresetEditor(fresh, -1); }});
            });
        ui::Button* exportAll = dialog->AddButton(u8"Export All", ui::DialogResult::None);
        exportAll->OnClick.Add(
            [this, raw](ui::ButtonBase*)
            {
                RunExport(StringView{}, true);
                raw->Close(ui::DialogResult::OK);
            });
        ui::Button* templates = dialog->AddButton(u8"Manage Templates...", ui::DialogResult::None);
        templates->OnClick.Add(
            [this, raw](ui::ButtonBase*)
            { QueueReplaceDialog(raw, Function<void()>{[this]() { OpenTemplatesManager(); }}); });
        dialog->AddButton(u8"Close", ui::DialogResult::Cancel);
        dialog->Show(&m_uiHost->Context());
    }

    void EditorApplication::OpenPresetEditor(editor::ExportPreset initial, isize editIndex)
    {
        if (!m_uiHost)
        {
            return;
        }

        editor::TemplateRegistry registry;
        BuildTemplateRegistryMainThread(registry);

        auto dialog = MakeRef<ui::Dialog>(
            DefaultAllocator(),
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
        comboIds.PushBack(String{});
        comboPlatforms.PushBack(String{});
        comboConfigs.PushBack(String{});
        i32 selectedCombo = 0;
        for (usize i = 0; i < registry.Count(); ++i)
        {
            const editor::ExportTemplate* t = registry.At(i);
            if (t == nullptr)
            {
                continue;
            }
            String item(t->name.AsView());
            item += u8" [";
            item += t->platform.AsView();
            item += u8"/";
            item += t->EffectiveConfig();
            item += u8"]";
            if (t->isHost)
            {
                item += u8" (host)";
            }
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
            browse->OnClick.Add([this, filesRef](ui::ButtonBase*)
                                { PickAdditionalFiles(filesRef); });
            filesRow->AddView(browse.Get());
        }

        auto symbolsCheck = MakeRef<ui::CheckBox>(DefaultAllocator(),
                                                  StringView(u8"Stage debug symbols into the dist"),
                                                  initial.stageSymbols);
        column->AddView(symbolsCheck.Get());
        auto pruneCheck = MakeRef<ui::CheckBox>(DefaultAllocator(),
                                                StringView(u8"Prune to reachable content only"),
                                                initial.pruneToReachable);
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
        save->OnClick.Add(
            [this, raw, editIndex, nameRaw, comboRaw, platformRaw, configRaw, playerRaw, subdirRaw,
             filesRaw, symbolsRaw, pruneRaw, comboIds, comboPlatforms,
             comboConfigs](ui::ButtonBase*)
            {
                editor::ExportPreset result;
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

                if (editIndex < 0)
                {
                    m_presetsController.Add(result);
                }
                else
                {
                    m_presetsController.Update(static_cast<usize>(editIndex), result);
                }
                SavePresetsController();
                ReopenExportPresetsPanel(raw);
            });
        ui::Button* cancel = dialog->AddButton(u8"Cancel", ui::DialogResult::None);
        cancel->OnClick.Add([this, raw](ui::ButtonBase*) { ReopenExportPresetsPanel(raw); });
        dialog->Show(&m_uiHost->Context());
    }

    void EditorApplication::SaveActivePageAs()
    {
        editor::EditorPage* page = m_context.ActivePage();
        if (page == nullptr || m_project.Get() == nullptr || m_uiHost.Get() == nullptr)
        {
            return;
        }
        draconic::content::Instance* original =
            m_project->SourceDb().GetInstance(page->InstanceId());
        if (original == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Warning,
                             u8"This page has no source asset to copy.");
            return;
        }
        const TypeInfo* type = GlobalTypeRegistry().FindByName(
            reinterpret_cast<const char*>(String(original->TypeNamespace()).CStr()),
            reinterpret_cast<const char*>(String(original->TypeName()).CStr()));
        if (type == nullptr)
        {
            m_context.Notify(editor::NoticeKind::Error, u8"Save As: unknown asset type.");
            return;
        }

        draconic::content::Group* group = &original->OwningGroup();
        String suggested(original->Name());
        suggested += u8" Copy";
        while (group->GetInstance(suggested.AsView()) != nullptr)
        {
            suggested += u8" Copy";
        }

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
        errorLabel->TextColor.SetValue(Color{0.90f, 0.35f, 0.35f, 1.0f});
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
        draconic::ui::Button* save = dialog->AddButton(u8"Save", draconic::ui::DialogResult::None);
        save->OnClick.Add(
            [this, pageId, group, type, rawDialog, rawEdit, rawError](draconic::ui::ButtonBase*)
            {
                const StringView newName = rawEdit->Text();
                if (newName.IsEmpty())
                {
                    rawError->SetText(u8"NOT saved: enter a name.");
                    return; // dialog stays up for the retry
                }
                if (group->GetInstance(newName) != nullptr)
                {
                    rawError->SetText(u8"NOT saved: that name already exists in the group.");
                    return; // dialog stays up for the retry
                }
                // Re-resolve the page: the dialog is modal-ish but pages can close under it.
                editor::EditorPage* target = nullptr;
                for (const UniquePtr<editor::EditorPage>& open : m_context.OpenPages())
                {
                    if (open->InstanceId() == pageId)
                    {
                        target = open.Get();
                        break;
                    }
                }
                if (target == nullptr)
                {
                    rawDialog->Close(draconic::ui::DialogResult::Cancel);
                    return;
                }
                draconic::content::Instance* fresh = group->CreateInstance(newName, *type);
                if (fresh == nullptr)
                {
                    m_context.Notify(editor::NoticeKind::Error,
                                     u8"NOT saved: could not create the new asset.");
                    return;
                }
                target->OnSavedAs(*fresh);
                if (target->Save().IsOk())
                {
                    String message(u8"Saved as '");
                    message += fresh->Name();
                    message += u8"'.";
                    m_context.Notify(editor::NoticeKind::Success, message.AsView());
                }
                else
                {
                    m_context.Notify(editor::NoticeKind::Error, u8"Save As FAILED (see Console).");
                }
                rawDialog->Close(draconic::ui::DialogResult::OK);
            });
        dialog->AddButton(u8"Cancel", draconic::ui::DialogResult::Cancel);
        dialog->Show(&m_uiHost->Context());
    }

    void EditorApplication::ShowDirtyCloseDialog(UIEditorPage* page,
                                                 ui::toolkit::DockablePanel* panel)
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
        save->OnClick.Add(
            [this, page, panel, rawDialog](draconic::ui::ButtonBase*)
            {
                if (page->Save().IsOk())
                {
                    panel->OnCloseRequested.Invoke(panel);
                    rawDialog->Close(draconic::ui::DialogResult::OK);
                }
                else
                {
                    m_context.Notify(editor::NoticeKind::Error,
                                     u8"Save FAILED (see console) - page stays open.");
                    rawDialog->Close(draconic::ui::DialogResult::Cancel);
                }
            });
        draconic::ui::Button* discard =
            dialog->AddButton(u8"Discard", draconic::ui::DialogResult::None);
        discard->OnClick.Add(
            [panel, rawDialog](draconic::ui::ButtonBase*)
            {
                panel->OnCloseRequested.Invoke(panel);
                rawDialog->Close(draconic::ui::DialogResult::OK);
            });
        dialog->AddButton(u8"Cancel", draconic::ui::DialogResult::Cancel);
        dialog->Show(&m_uiHost->Context());
    }

    void EditorApplication::SyncPageTitles()
    {
        for (const PagePanel& entry : m_pagePanels)
        {
            String title;
            draconic::content::Instance* instance =
                m_project ? m_project->SourceDb().GetInstance(entry.page->InstanceId()) : nullptr;
            if (instance != nullptr)
            {
                title = String(instance->Name());
            }
            else
            {
                title = String(entry.page->Title());
            }
            if (entry.page->IsDirty())
            {
                title += u8" *";
            }
            if (entry.panel->Title() != title.AsView())
            {
                entry.panel->SetTitle(title.AsView());
            }
        }
    }

    void EditorApplication::DrainLog()
    {
        if (m_config.logBuffer == nullptr || m_shell.Console() == nullptr)
        {
            return;
        }
        m_pendingLog.Clear();
        m_logSequence = m_config.logBuffer->CollectSince(m_logSequence, m_pendingLog);
        for (const draconic::editor::EditorLogEntry& entry : m_pendingLog)
        {
            m_shell.Console()->AddEntry(entry.level, entry.category.AsView(),
                                        entry.message.AsView());
        }
    }

    void EditorApplication::OpenProject()
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
                m_project =
                    draconic::editor::EditorProject::Open(m_config.projectDirectory.AsView());
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

    void EditorApplication::SaveLayout()
    {
        if (m_project)
        {
            Array<Guid> pages;
            Guid activePage;
            for (const PagePanel& entry : m_pagePanels)
            {
                // Instance-less pages (the Game tab) don't persist in the page set - a
                // nil guid would just fail the restore lookup.
                if (entry.page->InstanceId().IsNil())
                {
                    continue;
                }
                pages.PushBack(entry.page->InstanceId());
                if (m_context.ActivePage() == entry.page)
                {
                    activePage = entry.page->InstanceId();
                }
            }
            (void)SaveOpenPages(m_project->EditorStateRoot().AsView(), pages, activePage);
        }
        if (m_project && m_shell.Docks() != nullptr)
        {
            (void)m_shell.SaveLayout(m_project->EditorStateRoot().AsView());
        }
    }

    void EditorApplication::BuildMenus()
    {
        ui::toolkit::MenuBar* bar = m_shell.Menus();
        // Menu order: File FIRST (muscle memory), Build after it, Help-style menus last.
        draconic::ui::ContextMenu* file = bar->AddMenu(u8"File");
        if (draconic::ui::ContextMenu* build = bar->AddMenu(u8"Build"))
        {
            build->AddItem(u8"Cook All", [this]() { m_cookService.RequestCook(false); });
            build->AddItem(u8"Rebuild All", [this]() { m_cookService.RequestCook(true); });
        }

        if (file != nullptr)
        {
            runtime::IApplicationHost* host = m_host;

            // File > New <creator> from the registry (per-subsystem editor modules).
            // Categorized creators (e.g. "Primitives") nest in a submenu of that name.
            Array<StringView> categories;
            for (const draconic::editor::EditorContext::AssetCreator& creator :
                 m_context.Creators())
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
                for (StringView c : categories)
                {
                    if (c == creator.category.AsView())
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    categories.PushBack(creator.category.AsView());
                }
            }
            for (StringView category : categories)
            {
                draconic::ui::MenuItem* submenuItem = file->AddSubmenu(category);
                auto* submenu = Cast<draconic::ui::ContextMenu>(submenuItem->Submenu.Get());
                if (submenu == nullptr)
                {
                    continue;
                }
                for (const draconic::editor::EditorContext::AssetCreator& creator :
                     m_context.Creators())
                {
                    if (creator.category.AsView() != category)
                    {
                        continue;
                    }
                    const auto* entry = &creator;
                    submenu->AddItem(creator.label.AsView(),
                                     [this, entry]() { CreateAndOpen(*entry); });
                }
            }
            if (!m_context.Creators().IsEmpty())
            {
                file->AddSeparator();
            }

            file->AddItem(u8"Save", [this]() { SaveActivePage(); });
            file->AddItem(u8"Save As...", [this]() { SaveActivePageAs(); });
            file->AddSeparator();
            file->AddItem(u8"Save Layout",
                          [this]()
                          {
                              SaveLayout();
                              m_context.SetStatus(u8"Layout saved.");
                          });
            file->AddSeparator();
            file->AddItem(u8"Project Settings...",
                          [this]()
                          {
                              if (m_project)
                              {
                                  auto dialog =
                                      MakeRef<ProjectSettingsDialog>(DefaultAllocator(), m_context);
                                  dialog->Show(&m_uiHost->Context());
                              }
                          });
            file->AddItem(u8"Preferences...",
                          [this]()
                          {
                              auto dialog = MakeRef<EditorPreferencesDialog>(
                                  DefaultAllocator(), m_context, m_editorSettings);
                              dialog->Show(&m_uiHost->Context());
                          });
            file->AddSeparator();
            file->AddItem(u8"Export...", [this]() { OpenExportPresetsPanel(); });
            file->AddItem(u8"Manage Templates...", [this]() { OpenTemplatesManager(); });
            file->AddItem(u8"Exit",
                          [this, host]()
                          {
                              if (host != nullptr && ConfirmExitAllowed())
                              {
                                  host->RequestExit();
                              }
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
            view->AddItem(u8"Reset Layout",
                          [this]()
                          {
                              m_shell.ResetLayout();
                              m_context.SetStatus(u8"Layout reset to default.");
                          });
        }

        if (draconic::ui::ContextMenu* help = bar->AddMenu(u8"Help"))
        {
            help->AddItem(u8"About",
                          [this]()
                          {
                              m_context.SetStatus(
                                  u8"Draconic Editor - phase 1 shell (docs/design/editor.md)");
                          });
        }
    }
}
