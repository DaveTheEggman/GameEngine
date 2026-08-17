// AssetPickerSlot tests (asset-picker-slot.md P1). The composite slot is pure widget logic and
// exercises headlessly: affordances render only when their callback is WIRED (correction C1 -
// the entity-ref twin degrades to a plain name button), Edit/Clear/preview disable while the
// slot is empty, and each affordance fires its callback exactly once per click.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.ui;
import editor.core;
import editor.app;

using namespace foundation::core;
using namespace editor;
namespace ui = foundation::ui;

TEST_CASE("asset-slot: affordances render only when wired")
{
    // Nothing wired: a plain name button (the entity-ref degradation).
    auto bare = MakeRef<app::AssetPickerSlot>(DefaultAllocator(), StringView(u8"Thing"));
    bare->SetValue(u8"Thing", true);
    CHECK(bare->EditButton()->Visibility == ui::Visibility::Gone);
    CHECK(bare->PickButton()->Visibility == ui::Visibility::Gone);
    CHECK(bare->ClearButton()->Visibility == ui::Visibility::Gone);
    CHECK(bare->PreviewButton()->Visibility == ui::Visibility::Gone);
    CHECK(bare->BodyButton()->Visibility == ui::Visibility::Visible);

    // Fully wired: everything visible.
    auto slot = MakeRef<app::AssetPickerSlot>(DefaultAllocator());
    slot->OnPick = []() {};
    slot->OnEdit = []() {};
    slot->OnClear = []() {};
    slot->OnReveal = []() {};
    slot->SetValue(u8"MyClip", true);
    CHECK(slot->PickButton()->Visibility == ui::Visibility::Visible);
    CHECK(slot->EditButton()->Visibility == ui::Visibility::Visible);
    CHECK(slot->ClearButton()->Visibility == ui::Visibility::Visible);
    CHECK(slot->PreviewButton()->Visibility == ui::Visibility::Visible);
}

TEST_CASE("asset-slot: Edit/Clear/preview inert while empty, body always live")
{
    auto slot = MakeRef<app::AssetPickerSlot>(DefaultAllocator());
    slot->OnPick = []() {};
    slot->OnEdit = []() {};
    slot->OnClear = []() {};
    slot->OnReveal = []() {};

    slot->SetValue({}, false);
    CHECK(slot->BodyButton()->IsEnabled);
    CHECK(slot->PickButton()->IsEnabled); // picking an EMPTY slot is the point
    CHECK(!slot->EditButton()->IsEnabled);
    CHECK(!slot->ClearButton()->IsEnabled);
    CHECK(!slot->PreviewButton()->IsEnabled);

    slot->SetValue(u8"MyClip", true);
    CHECK(slot->EditButton()->IsEnabled);
    CHECK(slot->ClearButton()->IsEnabled);
    CHECK(slot->PreviewButton()->IsEnabled);
}

TEST_CASE("asset-slot: each affordance fires exactly once per click")
{
    auto slot = MakeRef<app::AssetPickerSlot>(DefaultAllocator());
    int picks = 0, edits = 0, clears = 0, reveals = 0;
    slot->OnPick = [&picks]() { ++picks; };
    slot->OnEdit = [&edits]() { ++edits; };
    slot->OnClear = [&clears]() { ++clears; };
    slot->OnReveal = [&reveals]() { ++reveals; };
    slot->SetValue(u8"MyClip", true);

    slot->BodyButton()->FireClick();
    slot->PickButton()->FireClick();
    slot->EditButton()->FireClick();
    slot->ClearButton()->FireClick();
    slot->PreviewButton()->FireClick();
    CHECK(picks == 2); // body + the dedicated Pick button both pick
    CHECK(edits == 1);
    CHECK(clears == 1);
    CHECK(reveals == 1);
}

TEST_CASE("asset-slot: empty text renders the (none) placeholder")
{
    auto slot = MakeRef<app::AssetPickerSlot>(DefaultAllocator());
    slot->SetValue({}, false);
    CHECK(slot->BodyButton()->Text.Value().AsView() == StringView(u8"(none)"));
    slot->SetValue(u8"Grass", true);
    CHECK(slot->BodyButton()->Text.Value().AsView() == StringView(u8"Grass"));
}

TEST_CASE("asset-slot: drop accepts matching types and rejects mismatches (P2)")
{
    auto slot = MakeRef<app::AssetPickerSlot>(DefaultAllocator());
    Array<String> accepted;
    accepted.PushBack(String(u8"TextureAsset"));
    slot->SetAcceptedTypes(Move(accepted));

    Guid assigned{};
    int assigns = 0, rejects = 0;
    String rejectedType;
    slot->OnAssignDropped = [&](const Guid& id)
    {
        assigned = id;
        ++assigns;
    };
    slot->OnRejectedDrop = [&](StringView, StringView typeName)
    {
        ++rejects;
        rejectedType = String(typeName);
    };

    // Not a drop target without accepted types; a drop target with them.
    CHECK(slot->AsDropTarget() == slot.Get());

    // Matching type: hover accepts, drop assigns exactly once.
    const Guid id{0x1234, 0x5678};
    auto match = MakeRef<app::AssetDragData>(DefaultAllocator(), id, StringView(u8"TextureAsset"),
                                             StringView(u8"Grass"));
    CHECK(slot->CanAcceptDrop(match.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(slot->OnDrop(match.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(assigns == 1);
    CHECK(assigned == id);
    CHECK(rejects == 0);

    // Wrong type: hover still engages (so OnDrop can warn), drop REJECTS - no assign.
    auto wrong = MakeRef<app::AssetDragData>(DefaultAllocator(), Guid{0x9abc, 0xdef0},
                                             StringView(u8"AudioClipAsset"),
                                             StringView(u8"Boom"));
    CHECK(slot->CanAcceptDrop(wrong.Get(), 0, 0) == ui::DragDropEffects::Link);
    CHECK(slot->OnDrop(wrong.Get(), 0, 0) == ui::DragDropEffects::None);
    CHECK(assigns == 1);
    CHECK(rejects == 1);
    CHECK(rejectedType.AsView() == StringView(u8"AudioClipAsset"));

    // A non-asset payload never engages.
    auto foreign = MakeRef<ui::DragData>(DefaultAllocator(), StringView(u8"view/reorder"));
    CHECK(slot->CanAcceptDrop(foreign.Get(), 0, 0) == ui::DragDropEffects::None);
}

TEST_CASE("asset-slot: no accepted types = not a drop target")
{
    auto slot = MakeRef<app::AssetPickerSlot>(DefaultAllocator());
    CHECK(slot->AsDropTarget() == nullptr);
}
