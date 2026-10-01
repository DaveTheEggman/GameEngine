// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The one list widget and the one asset row (editor-lists-and-asset-slots.md P0), headless,
// ported from Sedulous d6018d3f: a drop on a slot assigns it, a drop on the list appends, a wrong
// type is refused and reported, the add icon appends or defers to its menu, a section list is its
// header alone with element icons for the sections, and a ResourceRefEditor's pick, drop and clear
// are one assignment.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace editor;
namespace ui = foundation::ui;

namespace
{
    const Guid kMaterial{0x1111, 0x1};
    const Guid kTexture{0x2222, 0x2};

    RefPtr<app::AssetDragData> Drag(const Guid& id, StringView typeName)
    {
        return MakeRef<app::AssetDragData>(DefaultAllocator(), id, typeName, StringView(u8"dragged"));
    }

    // The slot of element `index`: the column's rows follow the header.
    app::AssetPickerSlot* SlotAt(ui::View* column, usize index)
    {
        auto* row = Cast<ui::ViewGroup>(Cast<ui::ViewGroup>(column)->GetChildAt(1 + index));
        return Cast<app::AssetPickerSlot>(row->GetChildAt(0));
    }

    ui::IconButton* AddIcon(ui::View* column)
    {
        auto* header = Cast<ui::ViewGroup>(Cast<ui::ViewGroup>(column)->GetChildAt(0));
        return Cast<ui::IconButton>(header->GetChildAt(1));
    }

    Array<String> Types(StringView type)
    {
        Array<String> types;
        types.PushBack(String(type));
        return types;
    }
}

TEST_CASE("container-list: a drop on a slot assigns it, a drop on the list appends, a wrong type "
          "is refused on both and reported")
{
    auto list = MakeRef<app::ContainerListEditor>(DefaultAllocator(), StringView(u8"Materials"),
                                                  StringView(u8"Mesh"));
    list->slotNames.PushBack(String(u8"Red"));
    list->slotNames.PushBack(String(u8"(none)"));
    list->SetAcceptedTypes(Types(u8"MaterialAsset"));
    usize assignedIndex = 99;
    Guid assigned;
    Guid appended;
    String rejected;
    list->OnAssignSlot = [&](usize i, const Guid& id)
    {
        assignedIndex = i;
        assigned = id;
    };
    list->OnAppendDropped = [&](const Guid& id) { appended = id; };
    list->OnRejectedDrop = [&](StringView, StringView typeName) { rejected = String(typeName); };
    ui::View* column = list->EditorView();

    // On slot 1: that element.
    app::AssetPickerSlot* slot = SlotAt(column, 1);
    REQUIRE(slot->AsDropTarget() != nullptr); // a typed list's slots are drop targets
    auto material = Drag(kMaterial, u8"MaterialAsset");
    CHECK(slot->OnDrop(material.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(assignedIndex == 1u);
    CHECK(assigned == kMaterial);

    // On the list itself (the header, a gap, an empty list): appended.
    ui::IDropTarget* zone = column->AsDropTarget();
    REQUIRE(zone != nullptr);
    CHECK(zone->OnDrop(material.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(appended == kMaterial);

    // A texture is refused on both, and reported.
    auto texture = Drag(kTexture, u8"TextureAsset");
    assignedIndex = 99;
    CHECK(slot->OnDrop(texture.Get(), 0, 0) == ui::DragDropEffects::None);
    CHECK(assignedIndex == 99u);
    CHECK(rejected == u8"TextureAsset");
    rejected = String();
    appended = Guid{};
    CHECK(zone->OnDrop(texture.Get(), 0, 0) == ui::DragDropEffects::None);
    CHECK(appended.IsNil());
    CHECK(rejected == u8"TextureAsset");
}

TEST_CASE("container-list: an untyped list takes no drop, and the wildcard takes any asset")
{
    auto untyped = MakeRef<app::ContainerListEditor>(DefaultAllocator(), StringView(u8"Things"),
                                                     StringView(u8"Cat"));
    untyped->slotNames.PushBack(String(u8"a"));
    untyped->OnAppendDropped = [](const Guid&) {};
    CHECK(SlotAt(untyped->EditorView(), 0)->AsDropTarget() == nullptr);
    CHECK(untyped->EditorView()->AsDropTarget() == nullptr);

    auto any = MakeRef<app::ContainerListEditor>(DefaultAllocator(), StringView(u8"Anything"),
                                                 StringView(u8"Cat"));
    any->slotNames.PushBack(String(u8"a"));
    any->SetAcceptedTypes(Types(app::AssetPickerSlot::kAnyAsset));
    Guid assigned;
    any->OnAssignSlot = [&](usize, const Guid& id) { assigned = id; };
    auto texture = Drag(kTexture, u8"TextureAsset");
    CHECK(SlotAt(any->EditorView(), 0)->OnDrop(texture.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(assigned == kTexture);
}

TEST_CASE("container-list: the add icon appends, or defers to its menu")
{
    auto list = MakeRef<app::ContainerListEditor>(DefaultAllocator(), StringView(u8"Things"),
                                                  StringView(u8"Cat"));
    i32 adds = 0;
    list->OnAdd = [&]() { ++adds; };
    AddIcon(list->EditorView())->FireClick();
    CHECK(adds == 1);
    CHECK(AddIcon(list->EditorView())->TooltipText == u8"Add");

    // With a menu, the icon opens it (headless: no context to open in) and does not append.
    auto menued = MakeRef<app::ContainerListEditor>(DefaultAllocator(), StringView(u8"Behaviors"),
                                                    StringView(u8"Cat"));
    i32 menuAdds = 0;
    menued->OnAdd = [&]() { ++menuAdds; };
    menued->OnAddMenu = [](ui::ContextMenu& menu) { menu.AddItem(u8"Kind", []() {}); };
    AddIcon(menued->EditorView())->FireClick();
    CHECK(menuAdds == 0);
}

TEST_CASE("container-list: a section list is its header, and its elements carry their own icons")
{
    auto list = MakeRef<app::ContainerListEditor>(DefaultAllocator(), StringView(u8"Behaviors"),
                                                  StringView(u8"Script"));
    list->ElementsAsSections = true;
    list->slotNames.PushBack(String(u8"Mover"));
    list->slotNames.PushBack(String(u8"Spinner"));
    auto* column = Cast<ui::ViewGroup>(list->EditorView());
    CHECK(column->ChildCount() == 1u); // the header alone: the elements are sections
    auto* count = Cast<ui::Label>(Cast<ui::ViewGroup>(column->GetChildAt(0))->GetChildAt(0));
    REQUIRE(count != nullptr);
    CHECK(count->Text.Value() == StringView(u8"2 items"));

    // A section's header icons: up, down, remove, each reporting its element.
    usize moved = 99;
    bool movedUp = true;
    usize removed = 99;
    RefPtr<ui::View> first = app::ContainerListEditor::ElementActions(
        DefaultAllocator(), 0, 2,
        [&](usize i, bool up)
        {
            moved = i;
            movedUp = up;
        },
        [&](usize i) { removed = i; });
    auto* firstGroup = Cast<ui::ViewGroup>(first.Get());
    CHECK_FALSE(Cast<ui::IconButton>(firstGroup->GetChildAt(0))->IsEnabled); // first: no up
    CHECK(Cast<ui::IconButton>(firstGroup->GetChildAt(1))->IsEnabled);
    Cast<ui::IconButton>(firstGroup->GetChildAt(1))->FireClick();
    CHECK(moved == 0u);
    CHECK_FALSE(movedUp);
    Cast<ui::IconButton>(firstGroup->GetChildAt(2))->FireClick();
    CHECK(removed == 0u);
    RefPtr<ui::View> last = app::ContainerListEditor::ElementActions(
        DefaultAllocator(), 1, 2, [](usize, bool) {}, [](usize) {});
    CHECK_FALSE(Cast<ui::IconButton>(Cast<ui::ViewGroup>(last.Get())->GetChildAt(1))->IsEnabled);

    // No move callback: remove alone (an order that means nothing).
    RefPtr<ui::View> removeOnly = app::ContainerListEditor::ElementActions(
        DefaultAllocator(), 0, 3, Function<void(usize, bool)>{}, [&](usize i) { removed = i + 10; });
    auto* removeGroup = Cast<ui::ViewGroup>(removeOnly.Get());
    CHECK(removeGroup->ChildCount() == 1u);
    Cast<ui::IconButton>(removeGroup->GetChildAt(0))->FireClick();
    CHECK(removed == 10u);
}

TEST_CASE("resource-row: pick, drop and clear are one assignment; a dangling id reads (missing)")
{
    EditorContext context{DefaultAllocator()};
    Guid current;
    Array<Guid> writes;
    const StringView types[] = {u8"MaterialAsset"};
    auto row = MakeRef<app::ResourceRefEditor>(DefaultAllocator(), StringView(u8"Material"),
                                               StringView(u8"(none)"), StringView(u8"Mesh"),
                                               Span<const StringView>{types, 1});
    row->BindAsset(context, [&]() { return current; },
                   [&](const Guid& id)
                   {
                       current = id;
                       writes.PushBack(id);
                   });
    auto* slot = Cast<app::AssetPickerSlot>(row->EditorView());
    REQUIRE(slot != nullptr);
    CHECK(slot->AsDropTarget() != nullptr); // a typed row is a drop target from construction

    // A drop assigns through the bound write; a wrong type does not.
    auto material = Drag(kMaterial, u8"MaterialAsset");
    CHECK(slot->OnDrop(material.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(writes.Size() == 1u);
    CHECK(current == kMaterial);
    auto texture = Drag(kTexture, u8"TextureAsset");
    CHECK(slot->OnDrop(texture.Get(), 0, 0) == ui::DragDropEffects::None);
    CHECK(writes.Size() == 1u);

    // Clear is the same write, with the nil id; the row names what it now holds.
    slot->ClearButton()->FireClick();
    CHECK(writes.Size() == 2u);
    CHECK(current.IsNil());
    CHECK(row->ValueText() == StringView(u8"(none)"));

    // A dangling id reads "(missing)" and stays clearable.
    current = kTexture;
    row->Refresh();
    CHECK(row->ValueText() == StringView(u8"(missing)"));
    CHECK(slot->ClearButton()->IsEnabled);

    // A row that is no asset (an entity reference) takes no asset drop.
    auto entityRow = MakeRef<app::ResourceRefEditor>(DefaultAllocator(), StringView(u8"Target"),
                                                     StringView(u8"(none)"), StringView(u8"Joint"),
                                                     Span<const StringView>{});
    CHECK(Cast<app::AssetPickerSlot>(entityRow->EditorView())->AsDropTarget() == nullptr);
}
