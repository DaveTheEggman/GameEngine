// Draconic::EditorApp - :application partition.
//
// EditorApplication: the editor as a runtime IApplication (docs/design/editor.md §3.2) - the
// UISandbox wiring, assembled for real: TrueType font service + UIHost + RuntimeDockableWindowHost
// (floating panels = borderless OS windows, drag-follow Tick) + the EditorShell chrome on the main
// window, with the EditorContext + EditorProject from draconic.editor.core underneath. Opens (or
// scaffolds) the project directory on startup, restores the per-user dock layout, saves it on
// shutdown. Phase 1: chrome + project only; pages/panels grow in later phases.

module;
#include "Core/Prelude.h"

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
import draconic.resource;
import draconic.editor;
import draconic.editor.core;
import :assets_view;
import :shell;
import :ui_page;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace rt = draconic::runtime;
    namespace graphics = draconic::graphics;
    namespace fonts = draconic::fonts;
    namespace uirt = draconic::ui::runtime;
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
                const f32 sizes[] = { 14.0f, 16.0f, 24.0f };
                for (f32 size : sizes)
                {
                    options.pixelHeight = size;
                    (void)m_fontService->LoadFont(u8"Roboto", m_config.fontPath.AsView(), options);
                }
            }

            m_uiHost = MakeUnique<uirt::UIHost>(DefaultAllocator(), *host.Graphics(), *host.Shell(), *m_fontService);
            m_dockHost = MakeUnique<uiapp::RuntimeDockableWindowHost>(DefaultAllocator(), host, *m_uiHost);

            // Theme: register the toolkit extension BEFORE creating the stylesheet (extensions
            // only apply to themes built afterward), then the editor defaults to dark.
            draconic::ui::ThemeRegistry::RegisterExtension(&m_toolkitTheme);
            m_styleSheet = draconic::ui::DarkTheme::Create();
            m_uiHost->Context().SetStyleSheet(m_styleSheet);

            m_shell.Build(m_context, m_dockHost.Get(), mainRw->Window().Width(), mainRw->Window().Height());
            m_uiHost->AttachWindow(mainRw, RefPtr<draconic::ui::RootView>(m_shell.Root()));

            OpenProject();

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
                m_assetsView = MakeRef<AssetsView>(DefaultAllocator(), m_context, m_cookService);
                AssetsView* assets = m_assetsView.Get();
                m_assetsView->OnOpenInstance = [this](draconic::content::Instance& instance) {
                    (void)OpenInstancePage(instance);
                };
                m_assetsView->OnCreate = [this](const draconic::editor::EditorContext::AssetCreator& creator) {
                    CreateAndOpen(creator);
                };
                m_cookService.OnCookFinished = [this, assets]() {
                    assets->Rebuild();
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

            // Reopen the project's default document (a full New Scene -> Save -> restart loop).
            if (m_project && !m_project->Settings().defaultScene.IsEmpty())
            {
                if (draconic::content::Instance* instance =
                        m_project->SourceDb().GetInstance(m_project->Settings().defaultScene.AsView()))
                {
                    (void)OpenInstancePage(*instance);
                }
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
            // (Docking activates the new tab - toolkit behavior since the dock-activates change.)
            // The DockManager's own close handling (wired in AddPanel) destroys the panel through
            // its deferred-delete queue; we additionally tear down the PAGE - deferred through the
            // UI mutation queue, since destroying views mid-event-dispatch is unsafe.
            panel->OnCloseRequested.Add([this, uiPage](tk::DockablePanel*) {
                m_uiHost->Context().MutationQueueRef().QueueAction(
                    Function<void()>{ [this, uiPage]() { ClosePage(uiPage); } });
            });
            m_pagePanels.PushBack(PagePanel{ uiPage, panel });
            return uiPage;
        }

        // Tear down a page whose panel is closing/closed (the DockManager owns panel
        // destruction; this handles only the page side).
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
            DrainLog();
            // Background-cook progress -> status bar (log lines reach the Console via the
            // logger); a finished cook refreshes the Assets badges through OnCookFinished.
            m_cookService.Update(Function<void(StringView)>{ [this](StringView line) {
                m_context.SetStatus(line);
            } });
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
        }

    private:
        // File > New <creator>: create the source instance, remember it as the project's default
        // document if none is set yet (so a fresh project reopens where you left off), open it.
        void CreateAndOpen(const draconic::editor::EditorContext::AssetCreator& creator)
        {
            draconic::content::Instance* instance = creator.create(m_context);
            if (instance == nullptr)
            {
                m_context.SetStatus(u8"Create failed (no project open?).");
                return;
            }
            if (m_project && m_project->Settings().defaultScene.IsEmpty())
            {
                m_project->Settings().defaultScene = instance->Path();
                (void)m_project->SaveSettings();
            }
            (void)OpenInstancePage(*instance);
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

            // Per-user layout (falls back to the built default on NotFound).
            (void)m_shell.RestoreLayout(m_project->EditorStateRoot().AsView());

            String message(u8"Project: ");
            message += m_project->Name();
            message += u8"  (";
            message += m_project->Directory();
            message += u8")";
            m_context.SetStatus(message.AsView());
        }

        void SaveLayout()
        {
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
                for (const draconic::editor::EditorContext::AssetCreator& creator : m_context.Creators())
                {
                    String label(u8"New ");
                    label += creator.label;
                    const auto* entry = &creator;
                    file->AddItem(label.AsView(), [this, entry]() { CreateAndOpen(*entry); });
                }
                if (!m_context.Creators().IsEmpty()) { file->AddSeparator(); }

                file->AddItem(u8"Save", [this]() {
                    if (auto* page = m_context.ActivePage())
                    {
                        m_context.SetStatus(page->Save().IsOk() ? StringView(u8"Saved.")
                                                                : StringView(u8"Save FAILED (see console)."));
                    }
                });
                file->AddSeparator();
                file->AddItem(u8"Save Layout", [this]() {
                    SaveLayout();
                    m_context.SetStatus(u8"Layout saved.");
                });
                file->AddItem(u8"Exit", [host]() { if (host != nullptr) { host->RequestExit(); } });
            }

            if (draconic::ui::ContextMenu* edit = bar->AddMenu(u8"Edit"))
            {
                edit->AddItem(u8"Undo", [this]() { m_context.Undo(); });
                edit->AddItem(u8"Redo", [this]() { m_context.Redo(); });
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
        Array<PagePanel> m_pagePanels;

        draconic::editor::EditorContext m_context;
        UniquePtr<draconic::editor::EditorProject> m_project;
        draconic::editor::BuilderRegistry m_builders;        // exe-assembled (registerEditors)
        draconic::editor::EditorCookService m_cookService;
        RefPtr<AssetsView> m_assetsView;
        f32 m_elapsed = 0.0f;   // autoExit/autoRebuild accumulator
        bool m_autoRebuilt = false;
        Array<draconic::shell::DroppedFile> m_droppedFiles;   // per-frame drain buffer
        Array<UniquePtr<draconic::resource::IResourceFactory>> m_resourceFactories;   // exe-assembled
        UniquePtr<draconic::resource::ResourceManager> m_resources;

        UniquePtr<fonts::TrueTypeFontService> m_fontService;
        tk::ToolkitThemeExtension m_toolkitTheme;
        RefPtr<draconic::ui::StyleSheet> m_styleSheet;
        EditorShell m_shell;
        UniquePtr<uiapp::RuntimeDockableWindowHost> m_dockHost;

        // The UI-on-runtime bridge (owns the UIContext, per-window VG + input). Declared LAST so
        // it tears down first (the shell's views outlive their windows inside the view tree).
        UniquePtr<uirt::UIHost> m_uiHost;
    };
}
