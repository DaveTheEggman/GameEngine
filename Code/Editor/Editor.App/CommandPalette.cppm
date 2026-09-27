// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::App - :command_palette partition.
//
// CommandPaletteDialog: every action by name. A filter box over the registry's declarations
// (label, description, id, ASCII case folded), the matches listed with their effective chord and
// menu path, Up/Down to move, Enter to run the highlighted one over the active subject through
// the registry (a disabled action stays listed greyed and refuses with a status line), Escape to
// close. Free once the registry exists: the palette is the registry, filtered.
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:command_palette;

import foundation.core;
import foundation.ui;
import editor.core;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;

    class CommandPaletteDialog final : public ui::Dialog
    {
        RTTI_OBJECT(CommandPaletteDialog, ui::Dialog)
    public:
        explicit CommandPaletteDialog(EditorActionRegistry& actions)
            : ui::Dialog(u8"Command Palette"), m_actions(&actions)
        {
            MinWidth.SetValue(520.0f);
            MinHeight.SetValue(360.0f);
            MaxWidth.SetValue(720.0f);
            MaxHeight.SetValue(520.0f);

            auto column = MakeRef<ui::FlexLayout>(MemoryAllocator());
            column->Direction = ui::Orientation::Vertical;
            column->Spacing = 6;

            // The filter first, so Show focuses it (the first focusable child) and typing starts
            // at once; Enter on it runs the highlighted row.
            m_filter = MakeRef<ui::EditText>(MemoryAllocator());
            m_filter->SetPlaceholder(u8"Type an action's name...");
            {
                CommandPaletteDialog* self = this;
                m_filter->OnTextChanged.Add([self](ui::EditText* edit) { self->SetFilter(edit->Text()); });
                m_filter->OnSubmit.Add([self](ui::EditText*) { (void)self->ExecuteHighlighted(); });
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                column->AddView(m_filter.Get(), lp);
            }

            m_adapter = MakeUnique<Adapter>(MemoryAllocator(), *this);
            m_list = MakeRef<ui::ListView>(MemoryAllocator());
            m_list->ItemHeight.SetValue(26.0f);
            m_list->SetAdapter(m_adapter.Get());
            {
                CommandPaletteDialog* self = this;
                // A click highlights; a double click runs.
                m_list->OnItemClicked.Add(
                    ui::Event<void(i32, i32, f32, f32)>::Handler{[self](i32 index, i32 clicks, f32, f32)
                                                                 {
                                                                     self->Highlight(index);
                                                                     if (clicks >= 2)
                                                                     {
                                                                         (void)self->ExecuteHighlighted();
                                                                     }
                                                                 }});
                ui::LayoutStyle lp;
                lp.Width = ui::SizeSpec::Match();
                lp.FlexGrow = 1.0f;
                column->AddView(m_list.Get(), lp);
            }

            m_status = MakeRef<ui::Label>(MemoryAllocator(), StringView());
            m_status->FontSize.SetValue(11.0f);
            m_status->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
            column->AddView(m_status.Get());

            SetContent(column.Get());
            Rebuild();
        }
        ~CommandPaletteDialog() override { m_list->SetAdapter(nullptr); }

        // === The model (what a test drives; the views follow) ===

        /// The rows for `filter`: every action whose label, description or id contains it
        /// (ASCII case folded), label matches first (a label starting with the filter before one
        /// merely containing it), then the rest, each group in registration order. An empty
        /// filter lists everything. The highlight goes back to the first row.
        void SetFilter(StringView filter)
        {
            m_filterText = String(filter);
            Rebuild();
        }
        [[nodiscard]] StringView Filter() const noexcept { return m_filterText.AsView(); }
        [[nodiscard]] Span<const EditorActionDeclaration* const> Rows() const noexcept
        {
            return Span<const EditorActionDeclaration* const>{m_rows.Data(), m_rows.Size()};
        }
        /// The highlighted row (-1 with no rows).
        [[nodiscard]] i32 Highlighted() const noexcept { return m_highlight; }
        void Highlight(i32 row)
        {
            if (m_rows.IsEmpty())
            {
                m_highlight = -1;
                return;
            }
            m_highlight = Clamp(row, 0, static_cast<i32>(m_rows.Size()) - 1);
            m_list->Selection.Select(m_highlight);
            m_list->ScrollToPosition(m_highlight);
        }
        /// Up and Down move the highlight, clamped at the ends.
        void MoveHighlight(i32 delta) { Highlight(m_highlight + delta); }
        /// Run the highlighted action over the active subject: the dialog closes on success;
        /// a refused one (disabled over the active page, or no row) says so in the status line
        /// and keeps the palette open. Returns the registry's status.
        [[nodiscard]] Status ExecuteHighlighted()
        {
            if (m_highlight < 0 || static_cast<usize>(m_highlight) >= m_rows.Size())
            {
                m_status->SetText(u8"Nothing matches.");
                return Status{ErrorCode::NotFound};
            }
            const EditorActionDeclaration* action = m_rows[static_cast<usize>(m_highlight)];
            const Status ran = m_actions->Execute(action->id.AsView());
            if (!ran.IsOk())
            {
                m_status->SetText(Format(u8"'{}' is not available over the active page.", action->label.AsView()).AsView());
                return ran;
            }
            Close(ui::DialogResult::OK);
            return ran;
        }

        // === Keys: before the filter box sees them (it handles every key itself) ===

        void OnKeyDownCapture(ui::KeyEventArgs& e) override
        {
            switch (e.Key)
            {
            case ui::KeyCode::Down:
                MoveHighlight(1);
                e.Handled = true;
                break;
            case ui::KeyCode::Up:
                MoveHighlight(-1);
                e.Handled = true;
                break;
            case ui::KeyCode::Return:
            case ui::KeyCode::KeypadEnter:
                (void)ExecuteHighlighted();
                e.Handled = true;
                break;
            default:
                break; // Escape: the dialog's own OnKeyDown closes it
            }
        }

    private:
        // One row: label | chord | menu path, the label greyed when the action is disabled now.
        class Row final : public ui::FlexLayout
        {
        public:
            Row()
            {
                Direction = ui::Orientation::Horizontal;
                Spacing = 12;
                label = MakeRef<ui::Label>(MemoryAllocator(), StringView());
                label->FontSize.SetValue(13.0f);
                {
                    ui::LayoutStyle lp;
                    lp.FlexGrow = 1.0f;
                    lp.AlignSelf = ui::Align::Center;
                    AddView(label.Get(), lp);
                }
                where = MakeRef<ui::Label>(MemoryAllocator(), StringView());
                where->FontSize.SetValue(11.0f);
                where->TextColor.SetValue(Optional<Color>(Color{0.55f, 0.55f, 0.55f, 1.0f}));
                {
                    ui::LayoutStyle lp;
                    lp.AlignSelf = ui::Align::Center;
                    AddView(where.Get(), lp);
                }
                chord = MakeRef<ui::Label>(MemoryAllocator(), StringView());
                chord->FontSize.SetValue(12.0f);
                chord->TextColor.SetValue(Optional<Color>(Color{0.7f, 0.7f, 0.7f, 1.0f}));
                {
                    ui::LayoutStyle lp;
                    lp.Width = ui::SizeSpec::Fixed(ui::Unit::Dp(110));
                    lp.AlignSelf = ui::Align::Center;
                    AddView(chord.Get(), lp);
                }
            }
            RefPtr<ui::Label> label;
            RefPtr<ui::Label> where;
            RefPtr<ui::Label> chord;
        };

        class Adapter final : public ui::ListAdapterBase
        {
        public:
            explicit Adapter(CommandPaletteDialog& owner) : m_owner(&owner) {}
            [[nodiscard]] i32 ItemCount() const override { return static_cast<i32>(m_owner->m_rows.Size()); }
            [[nodiscard]] RefPtr<ui::View> CreateView(i32) override
            {
                return MakeRef<Row>(m_owner->MemoryAllocator());
            }
            void BindView(ui::View* view, i32 position) override
            {
                auto* row = static_cast<Row*>(view); // the adapter made it: a Row
                if (row == nullptr || position < 0 || static_cast<usize>(position) >= m_owner->m_rows.Size())
                {
                    return;
                }
                const EditorActionDeclaration& action = *m_owner->m_rows[static_cast<usize>(position)];
                const bool enabled = m_owner->m_actions->IsEnabled(action.id.AsView());
                row->label->SetText(action.label.AsView());
                row->label->TextColor.SetValue(
                    enabled ? Optional<Color>() : Optional<Color>(Color{0.5f, 0.5f, 0.5f, 1.0f}));
                row->where->SetText(action.menuPath.AsView());
                row->chord->SetText(FormatShortcut(m_owner->m_actions->Shortcut(action.id.AsView())).AsView());
            }

        private:
            CommandPaletteDialog* m_owner;
        };

        void Rebuild()
        {
            m_rows.Clear();
            const StringView filter = m_filterText.AsView();
            Array<const EditorActionDeclaration*> contains;
            Array<const EditorActionDeclaration*> elsewhere;
            for (const EditorActionDeclaration& action : m_actions->Actions())
            {
                const StringView label = action.label.AsView();
                if (label.ContainsIgnoreCase(filter))
                {
                    if (filter.IsEmpty() || label.SubStr(0, filter.Size()).ContainsIgnoreCase(filter))
                    {
                        m_rows.PushBack(&action); // a label starting with the filter
                    }
                    else
                    {
                        contains.PushBack(&action);
                    }
                }
                else if (action.description.AsView().ContainsIgnoreCase(filter) ||
                         action.id.AsView().ContainsIgnoreCase(filter))
                {
                    elsewhere.PushBack(&action);
                }
            }
            for (const EditorActionDeclaration* action : contains)
            {
                m_rows.PushBack(action);
            }
            for (const EditorActionDeclaration* action : elsewhere)
            {
                m_rows.PushBack(action);
            }
            m_adapter->NotifyDataSetChanged();
            m_status->SetText(m_rows.IsEmpty() ? StringView(u8"Nothing matches.") : StringView());
            Highlight(0);
        }

        EditorActionRegistry* m_actions;
        String m_filterText;
        Array<const EditorActionDeclaration*> m_rows;
        i32 m_highlight = -1;
        RefPtr<ui::EditText> m_filter;
        RefPtr<ui::ListView> m_list;
        RefPtr<ui::Label> m_status;
        UniquePtr<Adapter> m_adapter;
    };

    RTTI_DEFINE_OBJECT(CommandPaletteDialog, "rtti::editor::editor::app")
}
