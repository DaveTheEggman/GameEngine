// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :page_toolbar partition.
//
// PageToolbar: a page's action bar, built from the action registry OVER THAT PAGE. The
// standard set the page asks for comes first (Edit: file.save / edit.undo / edit.redo /
// page.discardChanges; Save: file.save alone; None), then the playback transport for a page that
// plays something back (AddPlayback); a page adds its own domain actions by id (AddAction). Every button shows the declaration's label,
// executes through the registry with this page as the subject - not the active page, since a
// split layout shows two pages and only one is active - and Refresh() syncs enabled and
// checked from the registry's answer over this page. A page prepends this to the top of its
// ContentView and calls Refresh() each frame (cheap).
module;
#include "Core/Prelude.h"
export module editor.app:page_toolbar;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;

    class PageToolbar : public ui::toolkit::Toolbar
    {
    public:
        /// Which of the standard actions lead the bar.
        enum class Standard
        {
            /// Save, undo, redo and discard: a page whose edits are commands.
            Edit,
            /// Save alone: a text page, whose editor keeps its own undo and whose unsaved text
            /// is not a command stack to discard.
            Save,
            /// None: a page with nothing to save, only its own actions (an audition).
            None,
        };

        PageToolbar(editor::EditorPage& page, editor::EditorActionRegistry& actions,
                    Standard standard = Standard::Edit)
            : m_page(&page), m_actions(&actions)
        {
            if (standard != Standard::None)
            {
                AddAction(u8"file.save");
            }
            if (standard == Standard::Edit)
            {
                AddSeparator();
                AddAction(u8"edit.undo");
                AddAction(u8"edit.redo");
                AddSeparator();
                AddAction(u8"page.discardChanges");
            }
            Refresh();
        }

        /// The page's content under its toolbar: the column a page hands out as its
        /// ContentView.
        [[nodiscard]] static RefPtr<ui::View> Frame(IAllocator& allocator, PageToolbar& toolbar,
                                                    ui::View& content)
        {
            auto column = MakeRef<ui::FlexLayout>(allocator);
            column->Direction = ui::Orientation::Vertical;
            ui::LayoutStyle bar;
            bar.Width = ui::SizeSpec::Match();
            column->AddView(&toolbar, bar);
            ui::LayoutStyle grow;
            grow.FlexGrow = 1.0f;
            grow.Width = ui::SizeSpec::Match();
            column->AddView(&content, grow);
            return RefPtr<ui::View>(column.Get());
        }

        /// The playback transport, for a page that publishes IPlaybackPage: Play (checked while
        /// playing), Stop and Restart, after a separator when the bar has buttons already.
        void AddPlayback()
        {
            if (ChildCount() > 0)
            {
                AddSeparator();
            }
            AddAction(u8"playback.play");
            AddAction(u8"playback.stop");
            AddAction(u8"playback.restart");
            Refresh();
        }

        /// A button for the action `id`, over this page: a Command as a button, a Toggle or
        /// Window as a toggle showing the checked state. Null when no such action is
        /// registered (the page asked for a name its domain never declared; logged by the
        /// registry's Find being null is the page's mistake to see in the bar).
        ui::toolkit::ToolbarButton* AddAction(StringView id)
        {
            const editor::EditorActionDeclaration* action = m_actions->Find(id);
            if (action == nullptr)
            {
                return nullptr;
            }
            Bound bound;
            bound.id = action->id;
            if (action->kind == editor::EditorActionKind::Command)
            {
                bound.button = AddButton(action->label.AsView());
            }
            else
            {
                ui::toolkit::ToolbarToggle* toggle = AddToggle(action->label.AsView());
                bound.toggle = toggle;
                bound.button = toggle;
            }
            editor::EditorPage* page = m_page;
            editor::EditorActionRegistry* actions = m_actions;
            const String actionId = action->id;
            bound.button->OnClick.Add([page, actions, actionId](ui::toolkit::ToolbarButton*)
                                      { (void)actions->Execute(actionId.AsView(), page); });
            ui::toolkit::ToolbarButton* button = bound.button;
            m_bound.PushBack(Move(bound));
            return button;
        }

        /// Sync every button's enabled (and a toggle's checked) state to the registry's answer
        /// over this page. Cheap; call per frame from the page's OnUpdate.
        void Refresh()
        {
            for (Bound& bound : m_bound)
            {
                bound.button->IsEnabled = m_actions->IsEnabled(bound.id.AsView(), m_page);
                if (bound.toggle != nullptr)
                {
                    bound.toggle->SetIsChecked(m_actions->IsChecked(bound.id.AsView(), m_page));
                }
            }
        }

        [[nodiscard]] usize BoundCount() const noexcept { return m_bound.Size(); }
        /// The button bound to `id`, or null (a page that wants to decorate one; the tests).
        [[nodiscard]] ui::toolkit::ToolbarButton* ButtonFor(StringView id) const noexcept
        {
            for (const Bound& bound : m_bound)
            {
                if (bound.id.AsView() == id)
                {
                    return bound.button;
                }
            }
            return nullptr;
        }

    private:
        struct Bound
        {
            String id;
            ui::toolkit::ToolbarButton* button = nullptr; // borrowed (toolbar-owned)
            ui::toolkit::ToolbarToggle* toggle = nullptr; // the same button when a toggle
        };

        editor::EditorPage* m_page;
        editor::EditorActionRegistry* m_actions;
        Array<Bound> m_bound;
    };
}
