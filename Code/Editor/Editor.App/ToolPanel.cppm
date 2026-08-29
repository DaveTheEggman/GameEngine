// Editor::App - the `editor.app:tool_panel` partition.
//
// The viewport-tool PANEL seam. A viewport tool (editor.viewport-
// tools) is a UI-free modal interaction mode; some tools are also "modes" that want an on-screen
// settings surface docked beside the viewport while they are active - property-animation authoring,
// a terrain-brush panel, a nav-mesh bake panel. That surface is UI (it builds ui::Views), so
// it CANNOT live on IViewportTool (the tool framework is deliberately UI-free, headless hosts link
// it). Instead this is a SECOND registry one layer up, in the editor-UI tier: UI-capable domain libs
// register an IViewportToolPanelProvider keyed by their tool's Id(); the scene page reserves a dock
// slot and, whenever the active tool changes, mounts the matching panel (or nothing).
//
// Navigation-ready by construction: the panel factory receives the SAME ViewportToolHostContext the
// tool did (scene + command stack + selection), and the panel's lifetime is ACTIVATION-SCOPED - the
// view is created on activate and dropped on deactivate, so durable edit state must live in the tool
// or its domain lib, never in the view. That keeps every panel safe to recreate and makes "add
// another mode" nothing more than "register another provider for another tool id".

module;
#include "Core/Prelude.h"

export module editor.app:tool_panel;

import foundation.core;
import foundation.ui;
import editor.viewporttools;

using namespace foundation::core;

export namespace editor
{
    /// Where the host should present a tool's panel. A HINT the provider declares; the host's mount
    /// callback decides how to honor each (and MAY fall back to Dock for one it does not implement).
    /// The framework stays presentation-agnostic - it only forwards the hint - so a page can host the
    /// SAME panel in the bottom dock, a floating palette, or a viewport overlay by routing on this.
    enum class ToolPanelPlacement : u8
    {
        Dock,            // a tab/slot in the editor's docked panel area (the default)
        Float,           // a floating palette near the viewport
        ViewportOverlay, // a HUD-style overlay drawn inside the viewport
    };

    /// A UI-tier settings panel for a viewport tool "mode". A UI-capable domain editor lib
    /// implements this and registers ONE per tool id; the host mounts the returned view while that
    /// tool is active. Static lifetime (the house rule: explicit registration, never discovery).
    class IViewportToolPanelProvider
    {
    public:
        virtual ~IViewportToolPanelProvider() = default;

        /// The tool this panel belongs to - matches IViewportTool::Id(). Stable, never localized.
        [[nodiscard]] virtual StringView ToolId() const = 0;

        /// Preferred presentation for this tool's panel (default Dock). The host forwards it to its
        /// mount callback; a provider overrides to Float/ViewportOverlay to render differently.
        [[nodiscard]] virtual ToolPanelPlacement Placement() const { return ToolPanelPlacement::Dock; }

        /// Build the panel view for one host activation. Receives the ACTIVE tool this panel is for
        /// (a provider downcasts it to its concrete tool - the id matched, so the type is known - to
        /// reach state the UI-free IViewportTool cannot expose) plus the same context the tool got.
        /// May return null (this context has nothing to show) - the host then docks no panel.
        [[nodiscard]] virtual RefPtr<foundation::ui::View>
        CreatePanel(IViewportTool& tool, const ViewportToolHostContext& context) = 0;
    };

    /// Registry of tool-panel providers, keyed by tool id. A process-wide singleton like the tool
    /// PROVIDER registry it mirrors (ViewportToolProviderRegistry), but constructible standalone so
    /// tests can exercise a host against a private set of providers.
    class ViewportToolPanelRegistry
    {
    public:
        ViewportToolPanelRegistry() = default;

        /// The process-wide instance the scene page consumes.
        [[nodiscard]] static ViewportToolPanelRegistry& Get();

        /// Registers a provider (static lifetime, borrowed). Idempotent on the same pointer; a
        /// second provider for an already-registered tool id is ignored (first wins) so a
        /// double-registered registrar cannot silently swap a page's panel.
        void Register(IViewportToolPanelProvider* provider);

        [[nodiscard]] usize Count() const noexcept { return m_providers.Size(); }

        /// The provider for a tool id, or null when that tool has no panel.
        [[nodiscard]] IViewportToolPanelProvider* FindByToolId(StringView toolId) noexcept;

    private:
        Array<IViewportToolPanelProvider*> m_providers;
    };

    /// Watches a ViewportToolManager's active tool and mounts/unmounts the matching panel through
    /// host-supplied callbacks. Pure control logic (no widget of its own) so it is unit-testable
    /// without a live viewport: the host owns the actual dock slot and decides how to mount (and
    /// whether to route the mount through its UI mutation queue). Call Sync() once per frame from
    /// the host's update (NOT mid-event-dispatch - the mount tears down the previous view).
    class ViewportToolPanelHost
    {
    public:
        /// `mount` receives the freshly built panel view + the provider's placement hint (so the host
        /// can route dock/float/overlay); `clear` is asked to remove whatever `mount` last showed, and
        /// is given that panel's placement so it tears down the RIGHT target. Both fire only on an
        /// active-tool CHANGE.
        ViewportToolPanelHost(ViewportToolManager& tools, ViewportToolPanelRegistry& registry,
                              ViewportToolHostContext context,
                              Function<void(foundation::ui::View*, ToolPanelPlacement)> mount,
                              Function<void(ToolPanelPlacement)> clear);

        /// Re-mount the panel if the active tool changed since the last call; otherwise a no-op.
        void Sync();

        [[nodiscard]] foundation::ui::View* CurrentPanel() const noexcept { return m_current.Get(); }
        [[nodiscard]] StringView CurrentToolId() const noexcept { return m_currentId.AsView(); }

    private:
        ViewportToolManager* m_tools;
        ViewportToolPanelRegistry* m_registry;
        ViewportToolHostContext m_context;
        Function<void(foundation::ui::View*, ToolPanelPlacement)> m_mount;
        Function<void(ToolPanelPlacement)> m_clear;
        RefPtr<foundation::ui::View> m_current; // keeps the mounted view alive for its activation
        String m_currentId;                     // the active tool id at the last Sync ("" = none)
        ToolPanelPlacement m_currentPlacement =
            ToolPanelPlacement::Dock; // placement of the mounted panel (so clear tears down the right target)
    };
}
