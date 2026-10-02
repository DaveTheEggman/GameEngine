// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor App - :container_list_editor partition
//
// THE editable list of the editor (editor-lists-and-asset-slots.md D1): every list a user adds to,
// reorders and removes from is one of these, whatever its elements are, so every list looks and
// behaves the same. ONE property-grid row whose editor view is a header (the add icon, top right)
// over a row per element. Two element shapes:
// - SLOT elements (the default): each row an AssetPickerSlot that fills, plus move-up, move-down
//   and remove icon buttons. The mesh's materials, a list of entity references.
// - SECTION elements (ElementsAsSections): an element with several fields of its own (a script
//   behaviour, a clip event) is a collapsible grid section of its own, like a component, and its
//   move and remove icons sit in that section's header (ElementActions). This row is then the
//   list's header alone: the count and the add icon.
// Adding is the add icon, or its menu when OnAddMenu is set (add by kind). With accepted types
// set, a slot takes a dropped asset of those types (OnAssignSlot), and the header, a gap or an
// empty list takes one to APPEND (OnAppendDropped): dragging three materials in fills three slots.
// Fully callback-driven, no reflection or component coupling: the consumer wires the callbacks
// and sets slotNames before the row builds.
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:container_list_editor;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import :editor_icons;
import :asset_picker_slot;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class ContainerListEditor final : public ui::toolkit::PropertyEditor
    {
        RTTI_OBJECT(ContainerListEditor, ui::toolkit::PropertyEditor)
    public:
        Function<void(usize)> OnPickSlot;      // pick/assign the asset in slot i
        Function<void(usize)> OnRemoveSlot;     // remove slot i
        Function<void(usize, bool)> OnMoveSlot; // reorder slot i (true = up)
        Function<void()> OnAdd;                 // append a new (empty) element
        /// When set, the add icon opens a menu this fills (add by kind) instead of calling OnAdd.
        Function<void(ui::ContextMenu&)> OnAddMenu;
        /// A dropped asset of an accepted type landed on slot i.
        Function<void(usize, const Guid&)> OnAssignSlot;
        /// A dropped asset of an accepted type landed on the header, a gap or the empty list.
        Function<void(const Guid&)> OnAppendDropped;
        /// A dropped asset of another type: its display name and type name.
        Function<void(StringView, StringView)> OnRejectedDrop;
        /// Per-element display text, set before the row builds: a slot's value, or a section
        /// element's summary.
        Array<String> slotNames;
        /// The elements are grid sections of their own; this row is the list's header alone.
        bool ElementsAsSections = false;

        ContainerListEditor(StringView name, StringView category)
            : ui::toolkit::PropertyEditor(name, category)
        {
        }

        /// The asset types the slots and an append drop accept (AssetPickerSlot::AnyAssetType() for
        /// any); empty is no drop target.
        void SetAcceptedTypes(Array<String> types) { m_acceptedTypes = Move(types); }
        [[nodiscard]] Span<const String> AcceptedTypes() const noexcept
        {
            return Span<const String>{m_acceptedTypes.Data(), m_acceptedTypes.Size()};
        }

        void RefreshView() override {}

        /// The move-up, move-down and remove icons for a SECTION element's header: element
        /// `index` of `count`, built from `allocator` (the caller's). Hand the view to
        /// PropertyGrid::SetCategoryHeaderActions for the element's section. A null `onMove` is
        /// remove alone (an order that means nothing).
        [[nodiscard]] static RefPtr<ui::View> ElementActions(IAllocator& allocator, usize index,
                                                             usize count,
                                                             Function<void(usize, bool)> onMove,
                                                             Function<void(usize)> onRemove);

    protected:
        RefPtr<ui::View> CreateEditorView() override;

    private:
        friend class ContainerListAppendZone;
        void AddClicked(ui::IconButton& add);

        Array<String> m_acceptedTypes;
    };
}
