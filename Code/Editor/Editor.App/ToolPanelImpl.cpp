// Editor::App - implementation unit for the `editor.app:tool_panel` partition (panel registry +
// the active-tool -> panel mount controller).

module;
#include "Core/Prelude.h"

module editor.app;

import foundation.core;
import foundation.ui;
import editor.viewporttools;

using namespace foundation::core;

namespace editor
{
    ViewportToolPanelRegistry& ViewportToolPanelRegistry::Get()
    {
        static ViewportToolPanelRegistry instance;
        return instance;
    }

    void ViewportToolPanelRegistry::Register(IViewportToolPanelProvider* provider)
    {
        if (provider == nullptr)
        {
            return;
        }
        for (IViewportToolPanelProvider* existing : m_providers)
        {
            if (existing == provider)
            {
                return; // idempotent re-registration of the same provider
            }
            if (existing->ToolId() == provider->ToolId())
            {
                return; // a panel for this tool id already exists; first registration wins
            }
        }
        m_providers.PushBack(provider);
    }

    IViewportToolPanelProvider* ViewportToolPanelRegistry::FindByToolId(StringView toolId) noexcept
    {
        for (IViewportToolPanelProvider* provider : m_providers)
        {
            if (provider->ToolId() == toolId)
            {
                return provider;
            }
        }
        return nullptr;
    }

    ViewportToolPanelHost::ViewportToolPanelHost(ViewportToolManager& tools,
                                                 ViewportToolPanelRegistry& registry,
                                                 ViewportToolHostContext context,
                                                 Function<void(foundation::ui::View*)> mount,
                                                 Function<void()> clear)
        : m_tools(&tools), m_registry(&registry), m_context(context), m_mount(Move(mount)),
          m_clear(Move(clear))
    {
    }

    void ViewportToolPanelHost::Sync()
    {
        IViewportTool* active = m_tools->ActiveTool();
        const StringView activeId = active != nullptr ? active->Id() : StringView{};
        if (activeId == m_currentId.AsView())
        {
            return; // no active-tool change since the last Sync
        }

        // Tear down the panel from the tool we are leaving (if any), then build the one for the
        // tool we are entering. The teardown/build order is fixed so the host never has two panels
        // in its slot at once.
        if (m_current.Get() != nullptr)
        {
            m_clear();
            m_current = {};
        }
        m_currentId = String(activeId);

        if (!activeId.IsEmpty())
        {
            if (IViewportToolPanelProvider* provider = m_registry->FindByToolId(activeId))
            {
                RefPtr<foundation::ui::View> panel = provider->CreatePanel(m_context);
                if (panel.Get() != nullptr)
                {
                    m_current = panel;
                    m_mount(panel.Get());
                }
            }
        }
    }
}
