// Editor::App - :container_list_editor partition (implementation).
//
// The header add-icon + a slot row (AssetPickerSlot + move-up / move-down / remove icon buttons) per
// element. Purely generic: every affordance calls back into the consumer-wired handlers.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module editor.app;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::core;
namespace ui = foundation::ui;

namespace editor::app
{
    RefPtr<ui::View> ContainerListEditor::CreateEditorView()
    {
        auto column = MakeRef<ui::FlexLayout>(DefaultAllocator());
        column->Direction = ui::Orientation::Vertical;
        column->Spacing = 2.0f;

        ContainerListEditor* self = this;
        editor::app::EditorIcons& icons = editor::app::EditorIcons::Get();

        // Header: a spacer that grows + the add icon button pinned to the right.
        {
            auto header = MakeRef<ui::FlexLayout>(DefaultAllocator());
            header->Direction = ui::Orientation::Horizontal;
            auto spacer = MakeRef<ui::FlexLayout>(DefaultAllocator());
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                header->AddView(spacer.Get(), lp);
            }
            auto add = MakeRef<ui::IconButton>(DefaultAllocator(), icons.add.Get());
            add->OnClick.Add([self](ui::ButtonBase*)
                             {
                                 if (self->OnAdd)
                                 {
                                     self->OnAdd();
                                 }
                             });
            header->AddView(add.Get());
            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(22.0f));
            column->AddView(header.Get(), lp);
        }

        // Slot rows: a picker slot that fills + move-up / move-down / remove icon buttons.
        for (usize i = 0; i < slotNames.Size(); ++i)
        {
            auto row = MakeRef<ui::FlexLayout>(DefaultAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 4.0f;

            auto slot = MakeRef<editor::app::AssetPickerSlot>(DefaultAllocator(), slotNames[i].AsView());
            slot->SetFontSize(12.0f);
            slot->OnPick = [self, i]()
            {
                if (self->OnPickSlot)
                {
                    self->OnPickSlot(i);
                }
            };
            {
                auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
                lp->Grow = 1.0f;
                row->AddView(slot.Get(), lp);
            }
            auto up = MakeRef<ui::IconButton>(DefaultAllocator(), icons.moveUp.Get());
            up->IsEnabled = i > 0;
            up->OnClick.Add([self, i](ui::ButtonBase*)
                            {
                                if (self->OnMoveSlot)
                                {
                                    self->OnMoveSlot(i, true);
                                }
                            });
            row->AddView(up.Get());
            auto down = MakeRef<ui::IconButton>(DefaultAllocator(), icons.moveDown.Get());
            down->IsEnabled = i + 1 < slotNames.Size();
            down->OnClick.Add([self, i](ui::ButtonBase*)
                              {
                                  if (self->OnMoveSlot)
                                  {
                                      self->OnMoveSlot(i, false);
                                  }
                              });
            row->AddView(down.Get());
            auto remove = MakeRef<ui::IconButton>(DefaultAllocator(), icons.remove.Get());
            remove->OnClick.Add([self, i](ui::ButtonBase*)
                                {
                                    if (self->OnRemoveSlot)
                                    {
                                        self->OnRemoveSlot(i);
                                    }
                                });
            row->AddView(remove.Get());

            auto lp = MakeRef<ui::FlexLayoutParams>(DefaultAllocator());
            lp->Width = ui::SizeSpec::Match();
            lp->Height = ui::SizeSpec::Fixed(ui::Unit::Px(22.0f));
            column->AddView(row.Get(), lp);
        }
        return column;
    }

    RTTI_DEFINE_OBJECT(ContainerListEditor, "rtti::editor::app")
}
