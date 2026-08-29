// Editor::App - :page_toolbar partition.
//
// PageToolbar: a standard per-page action bar - Save / Undo / Redo / Discard Changes - plus a slot
// for page-specific buttons. Wired to the page's editor::EditorPage interface (Save(), the per-page
// command stack, and DiscardChanges()); Refresh() syncs the button enabled states to the page's
// dirty + undo/redo availability. A page prepends this to the top of its ContentView and calls
// Refresh() each frame (cheap).

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
        explicit PageToolbar(editor::EditorPage& page) : m_page(&page)
        {
            PageToolbar* self = this;
            m_save = AddButton(u8"Save");
            m_save->OnClick.Add([self](ui::toolkit::ToolbarButton*) { (void)self->m_page->Save(); });
            AddSeparator();
            m_undo = AddButton(u8"Undo");
            m_undo->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->m_page->Commands().Undo(); });
            m_redo = AddButton(u8"Redo");
            m_redo->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->m_page->Commands().Redo(); });
            AddSeparator();
            m_discard = AddButton(u8"Discard Changes");
            m_discard->OnClick.Add([self](ui::toolkit::ToolbarButton*) { self->m_page->DiscardChanges(); });
            Refresh();
        }

        // A page-specific action, added to the right of the standard set (e.g. "Audition").
        ui::toolkit::ToolbarButton* AddPageButton(StringView label, Function<void()> onClick)
        {
            if (!m_pageSlotOpen)
            {
                AddSeparator();
                m_pageSlotOpen = true;
            }
            ui::toolkit::ToolbarButton* btn = AddButton(label);
            btn->OnClick.Add([fn = Move(onClick)](ui::toolkit::ToolbarButton*)
                             { if (fn) fn(); });
            return btn;
        }

        // Sync button enabled states to the page. Cheap; call per frame from the page's OnUpdate.
        void Refresh()
        {
            const bool dirty = m_page->IsDirty();
            m_save->IsEnabled = dirty;
            m_discard->IsEnabled = dirty;
            m_undo->IsEnabled = m_page->Commands().CanUndo();
            m_redo->IsEnabled = m_page->Commands().CanRedo();
        }

    private:
        editor::EditorPage* m_page;
        ui::toolkit::ToolbarButton* m_save = nullptr;
        ui::toolkit::ToolbarButton* m_undo = nullptr;
        ui::toolkit::ToolbarButton* m_redo = nullptr;
        ui::toolkit::ToolbarButton* m_discard = nullptr;
        bool m_pageSlotOpen = false;
    };
}
