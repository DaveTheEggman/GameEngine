// Draconic::EditorApp - :shell partition.
//
// EditorShell: the editor chrome (docs/design/editor.md §3.2) - a RootView holding
// [MenuBar / DockManager (grow) / StatusBar]. GLOBAL panels are Assets + Console only
// (Sedulous's split): everything scene-scoped (viewport, hierarchy, inspector, selection,
// camera) lives INSIDE each editor page, because multi-scene editing means several scene
// pages can be open at once - per-page views, never global panels (§3.6). The dock CENTER is
// the document area: each open page docks there as a closable tab via AddPagePanel; a
// non-closable Welcome panel holds the center until the first page opens (and keeps the
// center tab group alive when all pages close).

module;
#include "Core/Prelude.h"

export module draconic.editor.app:shell;

import draconic.core;
import draconic.ui;
import draconic.ui.toolkit;
import draconic.editor.core;
import :layout;
import :log_view;

using namespace draconic::core;

export namespace draconic::editor::app
{
    namespace ui = draconic::ui;
    namespace tk = draconic::ui::toolkit;

    // Stable persistence ids for the GLOBAL panels (LoadDockLayout match keys - do not rename).
    inline constexpr StringView kPanelWelcome = u8"welcome";
    inline constexpr StringView kPanelConsole = u8"console";
    inline constexpr StringView kPanelAssets  = u8"assets";

    class EditorShell
    {
    public:
        EditorShell() = default;
        EditorShell(const EditorShell&) = delete;
        EditorShell& operator=(const EditorShell&) = delete;

        // Build the chrome. `dockHost` (nullable) lets panels float into real OS windows.
        void Build(draconic::editor::EditorContext& context, tk::IDockableWindowHost* dockHost,
                   u32 width, u32 height)
        {
            m_root = MakeRef<ui::RootView>(DefaultAllocator());
            m_root->ViewportSize = Float2{ static_cast<f32>(width), static_cast<f32>(height) };
            m_root->DpiScale = 1.0f;

            auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
            column->Direction = ui::Orientation::Vertical;

            m_menuBar = MakeRef<tk::MenuBar>(DefaultAllocator());
            column->AddView(m_menuBar.Get());

            m_dock = MakeRef<tk::DockManager>(DefaultAllocator());
            m_dock->DockableWindowHost = dockHost;
            {
                auto grow = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                grow->Grow = 1.0f;
                column->AddView(m_dock.Get(), grow);
            }

            m_statusBar = MakeRef<tk::StatusBar>(DefaultAllocator());
            column->AddView(m_statusBar.Get());

            m_root->AddView(column.Get());

            BuildPanels();

            // Route context status text to the status bar.
            tk::StatusBar* status = m_statusBar.Get();
            context.OnStatus = [status](StringView text) { status->SetText(text); };
        }

        [[nodiscard]] ui::RootView* Root() const noexcept { return m_root.Get(); }
        [[nodiscard]] tk::MenuBar* Menus() const noexcept { return m_menuBar.Get(); }
        [[nodiscard]] tk::StatusBar* StatusBar() const noexcept { return m_statusBar.Get(); }
        [[nodiscard]] tk::DockManager* Docks() const noexcept { return m_dock.Get(); }

        [[nodiscard]] tk::DockablePanel* WelcomePanel() const noexcept { return m_welcome; }
        [[nodiscard]] tk::DockablePanel* ConsolePanel() const noexcept { return m_console; }
        [[nodiscard]] tk::DockablePanel* AssetsPanel() const noexcept { return m_assets; }

        /// The Console panel's log view (fed by the app's EditorLogBuffer drain).
        [[nodiscard]] LogView* Console() const noexcept { return m_logView.Get(); }

        // === Document area ===

        /// Dock a page's content view as a closable tab in the center document area (tabbed
        /// with the other open pages). The returned panel is owned by the DockManager;
        /// `onCloseRequested` is where the app routes dirty-check + EditorContext::ClosePage.
        tk::DockablePanel* AddPagePanel(StringView title, ui::View* content)
        {
            tk::DockablePanel* panel = m_dock->AddPanel(title, content);
            panel->SetClosable(true);
            m_dock->DockPanelRelativeTo(panel, tk::DockPosition::Center, m_welcome->Parent);
            return panel;
        }

        // Per-user layout persistence (project Editor/ dir). Page panels carry no
        // PersistenceId, so only the global arrangement is captured/matched.
        [[nodiscard]] Status SaveLayout(StringView directory) const
        {
            return SaveDockLayout(*m_dock, directory);
        }
        [[nodiscard]] Status RestoreLayout(StringView directory) const
        {
            return LoadDockLayout(*m_dock, directory);
        }

        /// Rebuild the default arrangement (View > Reset Layout).
        void ResetLayout()
        {
            DockDefaults();
        }

    private:
        void BuildPanels()
        {
            m_welcome = m_dock->AddPanel(u8"Welcome",
                MakeRef<ui::Label>(DefaultAllocator(),
                    StringView(u8"Draconic Editor - open an asset to begin")).Get());
            m_welcome->SetPersistenceId(kPanelWelcome);
            m_welcome->SetClosable(false);

            m_logView = MakeRef<LogView>(DefaultAllocator());
            m_console = m_dock->AddPanel(u8"Console", m_logView.Get());
            m_console->SetPersistenceId(kPanelConsole);

            m_assets = m_dock->AddPanel(u8"Assets",
                MakeRef<ui::Label>(DefaultAllocator(), StringView(u8"Asset browser (phase 6)")).Get());
            m_assets->SetPersistenceId(kPanelAssets);

            DockDefaults();
        }

        void DockDefaults()
        {
            // Documents center, Console bottom, Assets tabbed with Console (Sedulous's
            // south-stacked tool pane; scene-scoped views live inside the pages).
            m_dock->DockPanel(m_welcome, tk::DockPosition::Center);
            m_dock->DockPanel(m_console, tk::DockPosition::Bottom);
            m_dock->DockPanelRelativeTo(m_assets, tk::DockPosition::Center, m_console->Parent);
        }

        RefPtr<ui::RootView> m_root;
        RefPtr<tk::MenuBar> m_menuBar;
        RefPtr<tk::StatusBar> m_statusBar;
        RefPtr<tk::DockManager> m_dock;
        RefPtr<LogView> m_logView;

        // Borrowed - the DockManager owns registered panels.
        tk::DockablePanel* m_welcome = nullptr;
        tk::DockablePanel* m_console = nullptr;
        tk::DockablePanel* m_assets = nullptr;
    };
}
