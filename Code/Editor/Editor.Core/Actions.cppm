// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::Core - :actions partition.
//
// Editor actions: one declaration per user command, and the registry every surface is built
// from - the menu bar (from menuPath), the shortcuts (from the effective chord), the toolbars
// (by id), the command palette, Preferences > Shortcuts, and the MCP action bridge. A surface
// never binds a command of its own; it asks the registry whether an action is enabled or
// checked and tells it to Execute. The declaration carries what every surface needs to show
// it and the three bindings that ARE the command: execute, enabled (pulled, never pushed) and
// checked (a Toggle or Window's state). Actions are nullary: anything that takes a parameter
// is a tool or a list-driven menu, not an action.
//
// Registration is explicit, in the composition roots (the application for the editor-wide
// set, each domain's Register<Domain>Editor for its own), like pages, creators and the MCP
// tool contributions. An id registered twice is a programming error at boot, refused and
// logged, so the surfaces never see two actions with one name.
module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

export module editor.core:actions;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace editor
{
    /// A keyboard chord: unset (no key) means the action has no shortcut.
    struct EditorShortcut
    {
        foundation::ui::KeyCode key = foundation::ui::KeyCode::Unknown;
        foundation::ui::KeyModifiers modifiers = foundation::ui::KeyModifiers::None;

        [[nodiscard]] constexpr bool IsSet() const noexcept
        {
            return key != foundation::ui::KeyCode::Unknown;
        }
        [[nodiscard]] constexpr bool operator==(const EditorShortcut& other) const noexcept
        {
            return key == other.key && modifiers == other.modifiers;
        }
    };

    enum class EditorActionKind : u8
    {
        Command, // does something
        Toggle,  // flips a state the surfaces show as checked
        Window,  // shows or hides a panel; checked while shown
    };

    struct EditorActionDeclaration
    {
        /// Stable, dotted: "file.save", "edit.undo", "scene.simulate.start". The identity the
        /// shortcut overrides, the palette and the MCP bridge use; never shown as a label.
        String id;
        String label;       // "Save" (menus, toolbars)
        String description; // "Save the active page" (palette, tooltips, MCP)
        /// The menu bar is generated from these: "File/Save", "Edit/Undo", "Scene/Simulate/
        /// Start". Empty = not in the menu bar. Items order by menuOrder within a menu, with a
        /// separator between order bands (hundreds).
        String menuPath;
        i32 menuOrder = 0;
        /// An icon by name, resolved by the surface that draws it; empty = text only.
        String icon;
        EditorShortcut shortcut; // the default; the user's override wins (Rebind)
        EditorActionKind kind = EditorActionKind::Command;
        /// Changes nothing (the MCP readOnlyHint; runs under a read-only editor).
        bool readOnly = false;

        Function<void()> execute;
        Function<bool()> enabled; // unset = always enabled
        Function<bool()> checked; // Toggle / Window only; unset = never checked
    };

    class EditorActionRegistry
    {
    public:
        EditorActionRegistry() = default;
        EditorActionRegistry(const EditorActionRegistry&) = delete;
        EditorActionRegistry& operator=(const EditorActionRegistry&) = delete;

        /// Registrations and rebinds; the surfaces that cache (the menu bar, the shortcut
        /// table) rebuild on it.
        Function<void()> OnActionsChanged;

        /// Register a declaration. Refused (false, logged) when the id or label is empty, when
        /// execute is unset, or when the id is already registered - each a mistake in a
        /// composition root, never a runtime condition.
        bool Register(EditorActionDeclaration declaration)
        {
            if (declaration.id.IsEmpty() || declaration.label.IsEmpty() || !declaration.execute)
            {
                LOG_ERROR(u8"Editor", u8"action '{}' refused: an id, a label and execute are required",
                          declaration.id.AsView());
                return false;
            }
            if (Find(declaration.id.AsView()) != nullptr)
            {
                LOG_ERROR(u8"Editor", u8"action '{}' registered twice; the first stands",
                          declaration.id.AsView());
                return false;
            }
            const u64 key = Hash<StringView>{}(declaration.id.AsView());
            m_actions.PushBack(static_cast<EditorActionDeclaration&&>(declaration));
            m_index.InsertOrAssign(key, m_actions.Size() - 1);
            NotifyChanged();
            return true;
        }

        [[nodiscard]] const EditorActionDeclaration* Find(StringView id) const noexcept
        {
            const usize* slot = m_index.Find(Hash<StringView>{}(id));
            if (slot != nullptr && m_actions[*slot].id.AsView() == id)
            {
                return &m_actions[*slot];
            }
            // A hash collision or an unknown id: the linear walk is the truth.
            for (const EditorActionDeclaration& action : m_actions)
            {
                if (action.id.AsView() == id)
                {
                    return &action;
                }
            }
            return nullptr;
        }

        /// Every action, in registration order.
        [[nodiscard]] Span<const EditorActionDeclaration> Actions() const noexcept
        {
            return Span<const EditorActionDeclaration>{m_actions.Data(), m_actions.Size()};
        }
        [[nodiscard]] usize Count() const noexcept { return m_actions.Size(); }

        /// Unknown ids are disabled and unchecked: a surface asking about a name nobody
        /// registered shows nothing runnable.
        [[nodiscard]] bool IsEnabled(StringView id) const
        {
            const EditorActionDeclaration* action = Find(id);
            return action != nullptr && IsEnabled(*action);
        }
        [[nodiscard]] static bool IsEnabled(const EditorActionDeclaration& action)
        {
            return !action.enabled || action.enabled();
        }
        [[nodiscard]] bool IsChecked(StringView id) const
        {
            const EditorActionDeclaration* action = Find(id);
            return action != nullptr && IsChecked(*action);
        }
        [[nodiscard]] static bool IsChecked(const EditorActionDeclaration& action)
        {
            return static_cast<bool>(action.checked) && action.checked();
        }

        /// The one funnel: a menu click, a chord, a toolbar click, the palette and the MCP
        /// bridge all execute through here. NotFound for an unknown id; NotSupported when the
        /// action is not enabled (the surfaces never offer a disabled action, so this is the
        /// bridge's refusal); Ok once execute ran.
        [[nodiscard]] Status Execute(StringView id) const
        {
            const EditorActionDeclaration* action = Find(id);
            if (action == nullptr)
            {
                return Status{ErrorCode::NotFound};
            }
            if (!IsEnabled(*action))
            {
                return Status{ErrorCode::NotSupported};
            }
            action->execute();
            return Status{};
        }

        /// The effective chord: the user's override when one is set (an unset override is
        /// "no shortcut" on purpose), else the declaration's default.
        [[nodiscard]] EditorShortcut Shortcut(StringView id) const
        {
            const EditorActionDeclaration* action = Find(id);
            if (action == nullptr)
            {
                return EditorShortcut{};
            }
            const EditorShortcut* override = m_overrides.Find(Hash<StringView>{}(id));
            return override != nullptr ? *override : action->shortcut;
        }
        /// The action holding a chord, effective bindings considered; null when free.
        [[nodiscard]] const EditorActionDeclaration* HolderOf(EditorShortcut chord) const
        {
            if (!chord.IsSet())
            {
                return nullptr;
            }
            for (const EditorActionDeclaration& action : m_actions)
            {
                if (Shortcut(action.id.AsView()) == chord)
                {
                    return &action;
                }
            }
            return nullptr;
        }
        /// Bind the user's chord to an action (an unset chord = no shortcut). NotFound for an
        /// unknown id; AlreadyExists when another action holds the chord, naming it in
        /// `holder` so the settings page can say so. The override persists with the settings
        /// (a later layer); the registry keeps it for the session.
        [[nodiscard]] Status Rebind(StringView id, EditorShortcut chord,
                                    const EditorActionDeclaration** holder = nullptr)
        {
            const EditorActionDeclaration* action = Find(id);
            if (action == nullptr)
            {
                return Status{ErrorCode::NotFound};
            }
            const EditorActionDeclaration* taken = HolderOf(chord);
            if (taken != nullptr && taken != action)
            {
                if (holder != nullptr)
                {
                    *holder = taken;
                }
                return Status{ErrorCode::AlreadyExists};
            }
            m_overrides.InsertOrAssign(Hash<StringView>{}(id), chord);
            NotifyChanged();
            return Status{};
        }
        /// Forget the user's override: the declaration's default chord applies again.
        void ResetShortcut(StringView id)
        {
            if (m_overrides.Remove(Hash<StringView>{}(id)))
            {
                NotifyChanged();
            }
        }
        [[nodiscard]] bool HasOverride(StringView id) const noexcept
        {
            return m_overrides.Find(Hash<StringView>{}(id)) != nullptr;
        }

    private:
        void NotifyChanged() const
        {
            if (OnActionsChanged)
            {
                OnActionsChanged();
            }
        }

        Array<EditorActionDeclaration> m_actions;
        HashMap<u64, usize> m_index;              // id hash -> index (verified on lookup)
        HashMap<u64, EditorShortcut> m_overrides; // id hash -> the user's chord
    };
}
