// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :action_shortcuts partition.
//
// ActionShortcuts: the global shortcuts GENERATED from the action registry. Every action
// with an effective chord (the user's override or the declaration's default) and every
// alternate chord becomes one global shortcut on the UI ShortcutManager whose callback is
// the registry's Execute - the one funnel, so a chord and a menu click are the same thing.
// Globals dispatch AFTER the focused view, and text controls mark their key-downs handled,
// so a focused textbox keeps Ctrl+Z for its own undo (focus first). Rebuilt on every
// registry change (a registration, a rebind), the previous bindings removed first.
module;
#include "Core/Prelude.h"

export module editor.app:action_shortcuts;

import foundation.core;
import foundation.ui;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    class ActionShortcuts
    {
    public:
        ActionShortcuts(foundation::ui::ShortcutManager& shortcuts, EditorActionRegistry& actions)
            : m_shortcuts(&shortcuts), m_actions(&actions)
        {
            ActionShortcuts* self = this;
            m_actions->OnActionsChanged.Add([self]() { self->Rebind(); });
            Rebind();
        }
        ActionShortcuts(const ActionShortcuts&) = delete;
        ActionShortcuts& operator=(const ActionShortcuts&) = delete;
        ~ActionShortcuts() { Unbind(); }

        /// The bindings as the registry answers now.
        void Rebind()
        {
            Unbind();
            for (const EditorActionDeclaration& action : m_actions->Actions())
            {
                Bind(action.id.AsView(), m_actions->Shortcut(action.id.AsView()));
                Bind(action.id.AsView(), action.alternateShortcut);
            }
        }
        [[nodiscard]] usize BoundCount() const noexcept { return m_bound.Size(); }

    private:
        void Bind(StringView id, EditorShortcut chord)
        {
            if (!chord.IsSet())
            {
                return;
            }
            const EditorActionRegistry* actions = m_actions;
            const String action(id);
            m_bound.PushBack(m_shortcuts->AddGlobal(
                chord.key, chord.modifiers,
                [actions, action]() { (void)actions->Execute(action.AsView()); }));
        }
        void Unbind()
        {
            for (foundation::ui::Shortcut* shortcut : m_bound)
            {
                m_shortcuts->Remove(shortcut);
            }
            m_bound.Clear();
        }

        foundation::ui::ShortcutManager* m_shortcuts;
        EditorActionRegistry* m_actions;
        Array<foundation::ui::Shortcut*> m_bound; // borrowed; the manager owns them
    };
}
