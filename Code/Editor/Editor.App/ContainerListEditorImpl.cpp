// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor::App - :container_list_editor partition (implementation).
//
// The header (the count for a section list, the add icon) over a slot row per element, the
// column itself an append drop zone, and the element icons a section list's sections carry.
// Purely generic: every affordance calls back into the consumer-wired handlers.
module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

module editor.app;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;

using namespace foundation::core;
namespace ui = foundation::ui;

namespace editor::app
{
    // The list's column: a drop anywhere on it outside a slot (the header, the gaps, an empty
    // list) APPENDS. A slot is the deeper target and takes its own drops first.
    class ContainerListAppendZone final : public ui::FlexLayout, public ui::IDropTarget
    {
    public:
        explicit ContainerListAppendZone(ContainerListEditor& owner) : m_owner(&owner) {}

        [[nodiscard]] ui::IDropTarget* AsDropTarget() override
        {
            return (m_owner->OnAppendDropped && !m_owner->m_acceptedTypes.IsEmpty()) ? this
                                                                                    : nullptr;
        }
        [[nodiscard]] ui::DragDropEffects CanAcceptDrop(ui::DragData* data, f32, f32) override
        {
            return Cast<AssetDragData>(data) != nullptr ? ui::DragDropEffects::Link
                                                        : ui::DragDropEffects::None;
        }
        void OnDragEnter(ui::DragData* data, f32, f32) override
        {
            auto* asset = Cast<AssetDragData>(data);
            m_hover = asset != nullptr;
            m_matches = asset != nullptr &&
                        AssetPickerSlot::Accepts(m_owner->AcceptedTypes(),
                                                 asset->AssetTypeName.AsView());
            Invalidate();
        }
        void OnDragOver(ui::DragData*, f32, f32) override {}
        void OnDragLeave(ui::DragData*) override
        {
            m_hover = false;
            Invalidate();
        }
        [[nodiscard]] ui::DragDropEffects OnDrop(ui::DragData* data, f32, f32) override
        {
            m_hover = false;
            Invalidate();
            auto* asset = Cast<AssetDragData>(data);
            if (asset == nullptr)
            {
                return ui::DragDropEffects::None;
            }
            if (!AssetPickerSlot::Accepts(m_owner->AcceptedTypes(), asset->AssetTypeName.AsView()))
            {
                LOG_WARNING(u8"Assets", u8"'{}' is a {} - this list does not accept it",
                            asset->DisplayName, asset->AssetTypeName);
                if (m_owner->OnRejectedDrop)
                {
                    m_owner->OnRejectedDrop(asset->DisplayName.AsView(),
                                            asset->AssetTypeName.AsView());
                }
                return ui::DragDropEffects::None;
            }
            m_owner->OnAppendDropped(asset->Id);
            return ui::DragDropEffects::Link;
        }

        void OnDraw(ui::UIDrawContext& ctx) override
        {
            ui::FlexLayout::OnDraw(ctx);
            if (m_hover)
            {
                // The slot's cues: match = accent ring, mismatch = error ring.
                const Color ring =
                    m_matches ? ResolveStyleColor(ui::StyleProperty::AccentColor,
                                                  Color{80.0f / 255.0f, 150.0f / 255.0f,
                                                        240.0f / 255.0f, 1.0f})
                              : ResolveStyleColor(ui::StyleProperty::ErrorColor,
                                                  Color{210.0f / 255.0f, 60.0f / 255.0f,
                                                        60.0f / 255.0f, 1.0f});
                ctx.VG().StrokeRect(Rectangle{0, 0, Width(), Height()}, ring, 2.0f);
            }
        }

    private:
        ContainerListEditor* m_owner;
        bool m_hover = false;
        bool m_matches = false;
    };

    namespace
    {
        ui::LayoutStyle RowStyle()
        {
            ui::LayoutStyle lp;
            lp.Width = ui::SizeSpec::Match();
            lp.Height = ui::SizeSpec::Fixed(ui::Unit::Dp(22.0f));
            return lp;
        }

        // The move callback an element's up and down icons share.
        class SharedMove final : public RefCounted
        {
        public:
            explicit SharedMove(Function<void(usize, bool)> handler) : fn(Move(handler)) {}
            Function<void(usize, bool)> fn;
        };

        RefPtr<ui::IconButton> Icon(IAllocator& allocator, ui::SVGDrawable* drawable,
                                    StringView tooltip)
        {
            auto button = MakeRef<ui::IconButton>(allocator, drawable);
            button->TooltipText = String(tooltip);
            return button;
        }
    }

    RefPtr<ui::View> ContainerListEditor::CreateEditorView()
    {
        auto column = MakeRef<ContainerListAppendZone>(MemoryAllocator(), *this);
        column->Direction = ui::Orientation::Vertical;
        column->Spacing = 2.0f;
        ContainerListEditor* self = this;
        EditorIcons& icons = EditorIcons::Get();

        // The header: the count for a section list, a spacer that grows, the add icon on the right.
        {
            auto header = MakeRef<ui::FlexLayout>(MemoryAllocator());
            header->Direction = ui::Orientation::Horizontal;
            ui::LayoutStyle grow;
            grow.FlexGrow = 1.0f;
            grow.AlignSelf = ui::Align::Center;
            if (ElementsAsSections)
            {
                auto count = MakeRef<ui::Label>(
                    MemoryAllocator(),
                    Format(u8"{} {}", slotNames.Size(), slotNames.Size() == 1 ? u8"item" : u8"items")
                        .AsView());
                count->FontSize.SetValue(11.0f);
                header->AddView(count.Get(), grow);
            }
            else
            {
                auto spacer = MakeRef<ui::FlexLayout>(MemoryAllocator());
                header->AddView(spacer.Get(), grow);
            }
            RefPtr<ui::IconButton> add = Icon(MemoryAllocator(), icons.add.Get(), u8"Add");
            ui::IconButton* addRaw = add.Get();
            add->OnClick.Add([self, addRaw](ui::ButtonBase*) { self->AddClicked(*addRaw); });
            header->AddView(add.Get());
            column->AddView(header.Get(), RowStyle());
        }

        if (ElementsAsSections)
        {
            return column;
        }

        // The slot rows: a picker slot that fills plus move-up, move-down and remove.
        for (usize i = 0; i < slotNames.Size(); ++i)
        {
            auto row = MakeRef<ui::FlexLayout>(MemoryAllocator());
            row->Direction = ui::Orientation::Horizontal;
            row->Spacing = 4.0f;

            auto slot = MakeRef<AssetPickerSlot>(MemoryAllocator(), slotNames[i].AsView());
            slot->SetFontSize(12.0f);
            slot->OnPick = [self, i]()
            {
                if (self->OnPickSlot)
                {
                    self->OnPickSlot(i);
                }
            };
            slot->SetAcceptedTypes(m_acceptedTypes);
            slot->OnAssignDropped = [self, i](const Guid& id)
            {
                if (self->OnAssignSlot)
                {
                    self->OnAssignSlot(i, id);
                }
            };
            slot->OnRejectedDrop = [self](StringView assetName, StringView typeName)
            {
                if (self->OnRejectedDrop)
                {
                    self->OnRejectedDrop(assetName, typeName);
                }
            };
            {
                ui::LayoutStyle lp;
                lp.FlexGrow = 1.0f;
                row->AddView(slot.Get(), lp);
            }

            RefPtr<ui::IconButton> up = Icon(MemoryAllocator(), icons.moveUp.Get(), u8"Move up");
            up->IsEnabled = i > 0;
            up->OnClick.Add([self, i](ui::ButtonBase*)
                            {
                                if (self->OnMoveSlot)
                                {
                                    self->OnMoveSlot(i, true);
                                }
                            });
            row->AddView(up.Get());
            RefPtr<ui::IconButton> down =
                Icon(MemoryAllocator(), icons.moveDown.Get(), u8"Move down");
            down->IsEnabled = i + 1 < slotNames.Size();
            down->OnClick.Add([self, i](ui::ButtonBase*)
                              {
                                  if (self->OnMoveSlot)
                                  {
                                      self->OnMoveSlot(i, false);
                                  }
                              });
            row->AddView(down.Get());
            RefPtr<ui::IconButton> remove = Icon(MemoryAllocator(), icons.remove.Get(), u8"Remove");
            remove->OnClick.Add([self, i](ui::ButtonBase*)
                                {
                                    if (self->OnRemoveSlot)
                                    {
                                        self->OnRemoveSlot(i);
                                    }
                                });
            row->AddView(remove.Get());

            column->AddView(row.Get(), RowStyle());
        }
        return column;
    }

    void ContainerListEditor::AddClicked(ui::IconButton& add)
    {
        if (!OnAddMenu)
        {
            if (OnAdd)
            {
                OnAdd();
            }
            return;
        }
        ui::UIContext* context = add.Context;
        if (context == nullptr)
        {
            return; // headless: no context to open the menu in
        }
        auto menu = MakeRef<ui::ContextMenu>(MemoryAllocator());
        OnAddMenu(*menu);
        const Float2 at = add.LocalToScreen(Float2{0.0f, add.Height()});
        menu->Show(context, at.x, at.y);
    }

    RefPtr<ui::View> ContainerListEditor::ElementActions(IAllocator& allocator, usize index,
                                                         usize count,
                                                         Function<void(usize, bool)> onMove,
                                                         Function<void(usize)> onRemove)
    {
        auto actions = MakeRef<ui::FlexLayout>(allocator);
        actions->Direction = ui::Orientation::Horizontal;
        actions->Spacing = 2.0f;
        EditorIcons& icons = EditorIcons::Get();
        if (onMove)
        {
            // The two buttons share the callback through a ref-counted holder.
            RefPtr<SharedMove> move = MakeRef<SharedMove>(allocator, Move(onMove));
            RefPtr<ui::IconButton> up = Icon(allocator, icons.moveUp.Get(), u8"Move up");
            up->IsEnabled = index > 0;
            up->OnClick.Add([move, index](ui::ButtonBase*) { move->fn(index, true); });
            actions->AddView(up.Get());
            RefPtr<ui::IconButton> down = Icon(allocator, icons.moveDown.Get(), u8"Move down");
            down->IsEnabled = index + 1 < count;
            down->OnClick.Add([move, index](ui::ButtonBase*) { move->fn(index, false); });
            actions->AddView(down.Get());
        }
        RefPtr<ui::IconButton> removeButton = Icon(allocator, icons.remove.Get(), u8"Remove");
        removeButton->OnClick.Add([onRemove = Move(onRemove), index](ui::ButtonBase*)
                                  {
                                      if (onRemove)
                                      {
                                          onRemove(index);
                                      }
                                  });
        actions->AddView(removeButton.Get());
        return RefPtr<ui::View>(actions.Get());
    }

    RTTI_DEFINE_OBJECT(ContainerListEditor, "rtti::editor::app")
}
