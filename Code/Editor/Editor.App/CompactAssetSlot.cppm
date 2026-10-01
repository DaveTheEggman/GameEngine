// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell
// Editor App - :compact_asset_slot partition
//
// CompactAssetSlot: an asset reference for a toolbar or a preview bar - a caption and the same
// slot every asset row uses, at bar height, so a preview's skeleton, mesh or material picks,
// takes a dropped asset of its type and clears exactly like an inspector row
// (editor-lists-and-asset-slots.md P1). Bind it through Editor().BindAsset, then Build().
module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:compact_asset_slot;

import foundation.core;
import foundation.ui;
import foundation.ui.toolkit;
import :asset_picker_slot;
import :resource_ref_editor;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class CompactAssetSlot final : public ui::FlexLayout
    {
        RTTI_OBJECT(CompactAssetSlot, ui::FlexLayout)
    public:
        CompactAssetSlot(StringView caption, Span<const StringView> acceptedTypes,
                         StringView emptyText = u8"(none)");

        /// The row whose slot this shows: bind it before Build().
        [[nodiscard]] ResourceRefEditor& Editor() noexcept { return *m_editor; }

        /// Adds the slot. Call after binding, which decides the slot's affordances.
        void Build();

    private:
        RefPtr<ResourceRefEditor> m_editor;
    };
}
