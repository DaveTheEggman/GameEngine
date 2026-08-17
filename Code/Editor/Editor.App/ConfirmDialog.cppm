// Editor::App - :confirm_dialog partition.
//
// ConfirmDialog: a small modal question with 2-3 labeled choices, delivered through
// OnChosen(index) BEFORE the dialog closes. First consumer: the property-animation panel's
// dirty guard (Save / Discard / Cancel before loading over a modified clip); reusable for any
// destructive-choice prompt. Escape = the LAST choice (conventionally Cancel).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:confirm_dialog;

import foundation.core;
import foundation.ui;

using namespace foundation::core;

export namespace editor::app
{
    namespace ui = foundation::ui;

    class ConfirmDialog final : public ui::Dialog
    {
        RTTI_OBJECT(ConfirmDialog, ui::Dialog)
    public:
        /// The chosen button's index into the ctor's `choices` (Escape/close = last index).
        Function<void(usize)> OnChosen;

        ConfirmDialog(StringView title, StringView message, Span<const StringView> choices)
            : ui::Dialog(title)
        {
            MinWidth.SetValue(340.0f);
            MaxWidth.SetValue(460.0f);

            auto label = MakeRef<ui::Label>(DefaultAllocator(), message);
            label->FontSize.SetValue(12.0f);
            label->WordWrap.SetValue(true);
            SetContent(label.Get());

            m_choiceCount = choices.Size();
            for (usize i = 0; i < choices.Size(); ++i)
            {
                ConfirmDialog* self = this;
                ui::Button* button = AddButton(choices[i], ui::DialogResult::None);
                button->OnClick.Add(
                    [self, i](ui::ButtonBase*)
                    {
                        self->Deliver(i);
                        self->Close(ui::DialogResult::OK);
                    });
            }
            // Any non-button dismissal (Escape) counts as the last choice.
            ConfirmDialog* self = this;
            OnClosed.Add(
                [self](ui::Dialog*, ui::DialogResult)
                {
                    if (!self->m_delivered && self->m_choiceCount > 0)
                    {
                        self->Deliver(self->m_choiceCount - 1);
                    }
                });
        }

    private:
        void Deliver(usize index)
        {
            if (m_delivered)
            {
                return;
            }
            m_delivered = true;
            if (OnChosen)
            {
                OnChosen(index);
            }
        }

        usize m_choiceCount = 0;
        bool m_delivered = false;
    };

    RTTI_DEFINE_OBJECT(ConfirmDialog, "rtti::editor")
}
