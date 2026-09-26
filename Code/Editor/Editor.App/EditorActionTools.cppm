// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :mcp_action_tools partition.
//
// The MCP bridge over the editor's ACTIONS - one generic surface for everything a user can do
// by command: action_list (every declaration with its state over the active page),
// action_state (one) and action_execute (through the registry, the one funnel). An agent
// never waits for a human: action_execute runs UNATTENDED - every dialog the action would
// open is kept off the screen and closed as cancelled (the accident-preventing answer), and
// the result names what was suppressed, with the note that the action then most likely did
// nothing and a dedicated tool or the user is the way forward (ezEngine's contract).
module;
#include "Core/Prelude.h"

export module editor.app:mcp_action_tools;

import foundation.core;
import foundation.ui;
import foundation.json;
import foundation.mcp;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    /// The live editor the action tools see: the registry through the context, and the UI
    /// context whose dialogs an unattended call suppresses (null = nothing to suppress, a
    /// headless test).
    struct ActionToolSeams
    {
        editor::EditorContext* context = nullptr;
        foundation::ui::UIContext* ui = nullptr;
    };

    /// The unattended scope: while alive, every dialog shown through `ui` is suppressed and
    /// recorded by title. Installs the context's DialogInterceptor and restores what was
    /// there (scopes nest).
    class UnattendedDialogs
    {
    public:
        explicit UnattendedDialogs(foundation::ui::UIContext* ui) : m_ui(ui)
        {
            if (m_ui == nullptr)
            {
                return;
            }
            m_previous = Move(m_ui->DialogInterceptor);
            UnattendedDialogs* self = this;
            m_ui->DialogInterceptor = [self](foundation::ui::Dialog& dialog)
            {
                self->m_suppressed.PushBack(dialog.Title);
                return false;
            };
        }
        ~UnattendedDialogs()
        {
            if (m_ui != nullptr)
            {
                m_ui->DialogInterceptor = Move(m_previous);
            }
        }
        UnattendedDialogs(const UnattendedDialogs&) = delete;
        UnattendedDialogs& operator=(const UnattendedDialogs&) = delete;

        [[nodiscard]] Span<const String> Suppressed() const noexcept
        {
            return Span<const String>{m_suppressed.Data(), m_suppressed.Size()};
        }

    private:
        foundation::ui::UIContext* m_ui;
        Function<bool(foundation::ui::Dialog&)> m_previous;
        Array<String> m_suppressed;
    };

    /// The number of tools RegisterActionTools registers (action_list / action_state /
    /// action_execute); a tripwire like kPageToolCount.
    inline constexpr usize kActionToolCount = 3;

    void RegisterActionTools(foundation::mcp::McpServer& server, ActionToolSeams seams);
}
