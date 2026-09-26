// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :action_menus partition.
//
// ActionMenuBar: the menu bar GENERATED from the action registry. Every action with a
// menuPath ("File/Save", "Scene/Simulate/Start") lands in the menu its first segment names,
// the rest as nested submenus; menus appear in the order their names first appear among the
// registrations (the application registers its set in menu order, domains add later; a menu
// holding only leading items comes after them); items
// order by menuOrder with a separator between order bands (hundreds). A menu's items are
// rebuilt when it opens (ContextMenu::OnOpening), so every item's enabled state is the
// registry's answer at that moment; every item executes through the registry, the one
// funnel. A menu may carry LEADING items that are not actions - a list-driven set such as
// File > New <creator> - through AddLeadingItems; they come first, then a separator.
module;
#include "Core/Prelude.h"

export module editor.app:action_menus;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    class ActionMenuBar
    {
    public:
        using LeadingItems = Function<void(foundation::ui::ContextMenu&)>;

        ActionMenuBar(foundation::ui::toolkit::MenuBar& bar, EditorActionRegistry& actions)
            : m_bar(&bar), m_actions(&actions)
        {
            ActionMenuBar* self = this;
            m_actions->OnActionsChanged.Add([self]() { self->Rebuild(); });
            Rebuild();
        }
        ActionMenuBar(const ActionMenuBar&) = delete;
        ActionMenuBar& operator=(const ActionMenuBar&) = delete;

        /// Items that are not actions, at the top of `menu` (created when absent): the
        /// list-driven sets. Rebuilt with the menu.
        void AddLeadingItems(StringView menu, LeadingItems items)
        {
            Leading entry;
            entry.menu = String(menu);
            entry.items = Move(items);
            m_leading.PushBack(Move(entry));
            Rebuild();
        }

        /// The whole bar from the registry: the menus and their current items.
        void Rebuild()
        {
            m_bar->ClearMenus();
            // Menus in the order their names first appear among the actions; a menu that
            // holds only leading items comes after them.
            for (const EditorActionDeclaration& action : m_actions->Actions())
            {
                if (!action.menuPath.IsEmpty())
                {
                    (void)MenuFor(TopLevel(action.menuPath.AsView()));
                }
            }
            for (const Leading& leading : m_leading)
            {
                (void)MenuFor(leading.menu.AsView());
            }
            for (usize i = 0; i < m_bar->MenuCount(); ++i)
            {
                foundation::ui::ContextMenu* menu = m_bar->MenuAt(i);
                const String title(m_bar->MenuTitle(i));
                ActionMenuBar* self = this;
                menu->OnOpening = [self, title](foundation::ui::ContextMenu& opening)
                { self->Fill(title.AsView(), opening); };
                Fill(title.AsView(), *menu);
            }
        }

        /// The items of one top-level menu, as the registry answers now.
        void Fill(StringView title, foundation::ui::ContextMenu& menu) const
        {
            menu.ClearItems();
            bool any = false;
            for (const Leading& leading : m_leading)
            {
                if (leading.menu.AsView() == title)
                {
                    leading.items(menu);
                    any = menu.ItemCount() > 0;
                }
            }
            // The actions of this menu, in menuOrder, nested by the rest of their path.
            Array<const EditorActionDeclaration*> ordered;
            for (const EditorActionDeclaration& action : m_actions->Actions())
            {
                if (!action.menuPath.IsEmpty() && TopLevel(action.menuPath.AsView()) == title)
                {
                    ordered.PushBack(&action);
                }
            }
            ordered.Sort([](const EditorActionDeclaration* a, const EditorActionDeclaration* b)
                         { return a->menuOrder < b->menuOrder; });
            if (any && !ordered.IsEmpty())
            {
                menu.AddSeparator();
            }
            i32 band = 0;
            bool first = true;
            for (const EditorActionDeclaration* action : ordered)
            {
                const i32 thisBand = action->menuOrder / 100;
                if (!first && thisBand != band)
                {
                    menu.AddSeparator();
                }
                band = thisBand;
                first = false;
                AddItem(menu, Rest(action->menuPath.AsView()), *action);
            }
        }

    private:
        struct Leading
        {
            String menu;
            LeadingItems items;
        };

        [[nodiscard]] static StringView TopLevel(StringView path) noexcept
        {
            for (usize i = 0; i < path.Size(); ++i)
            {
                if (path[i] == u8'/')
                {
                    return path.SubStr(0, i);
                }
            }
            return path;
        }
        [[nodiscard]] static StringView Rest(StringView path) noexcept
        {
            for (usize i = 0; i < path.Size(); ++i)
            {
                if (path[i] == u8'/')
                {
                    return path.SubStr(i + 1, path.Size() - i - 1);
                }
            }
            return StringView();
        }

        foundation::ui::ContextMenu* MenuFor(StringView title)
        {
            for (usize i = 0; i < m_bar->MenuCount(); ++i)
            {
                if (m_bar->MenuTitle(i) == title)
                {
                    return m_bar->MenuAt(i);
                }
            }
            return m_bar->AddMenu(title);
        }

        /// `path` is the part after the top-level menu: "Save", or "Simulate/Start" (a
        /// submenu "Simulate" holding "Start"; found when present, created when not).
        void AddItem(foundation::ui::ContextMenu& menu, StringView path,
                     const EditorActionDeclaration& action) const
        {
            const StringView head = TopLevel(path);
            const StringView tail = Rest(path);
            if (tail.IsEmpty())
            {
                const EditorActionRegistry* actions = m_actions;
                const String id = action.id;
                menu.AddItem(head, [actions, id]() { (void)actions->Execute(id.AsView()); },
                             EditorActionRegistry::IsEnabled(action));
                return;
            }
            foundation::ui::ContextMenu* submenu = nullptr;
            for (i32 i = 0; i < menu.ItemCount(); ++i)
            {
                const foundation::ui::MenuItem* item = menu.ItemAt(i);
                if (item->Submenu && item->Label.AsView() == head)
                {
                    submenu = Cast<foundation::ui::ContextMenu>(item->Submenu.Get());
                    break;
                }
            }
            if (submenu == nullptr)
            {
                foundation::ui::MenuItem* item = menu.AddSubmenu(head);
                submenu = Cast<foundation::ui::ContextMenu>(item->Submenu.Get());
            }
            if (submenu != nullptr)
            {
                AddItem(*submenu, tail, action);
            }
        }

        foundation::ui::toolkit::MenuBar* m_bar;
        EditorActionRegistry* m_actions;
        Array<Leading> m_leading;
    };
}
