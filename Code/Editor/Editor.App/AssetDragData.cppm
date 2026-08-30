// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor App - :asset_drag_data partition
//
// The typed drag payload for asset-browser drags: the dragged
// instance's Guid + its asset-type NAME (what drop targets filter on) + the display name (for
// reject messages and drag visuals). Format "asset/instance"; recover the subtype with
// core::Cast<AssetDragData>.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module editor.app:asset_drag_data;

import foundation.core;
import foundation.ui;

using namespace foundation::core;
namespace ui = foundation::ui;

export namespace editor::app
{
    class AssetDragData final : public ui::DragData
    {
        RTTI_OBJECT(AssetDragData, ui::DragData)
    public:
        AssetDragData(const Guid& id, StringView assetTypeName, StringView displayName)
            : ui::DragData(u8"asset/instance"), Id(id), AssetTypeName(assetTypeName),
              DisplayName(displayName)
        {
        }

        Guid Id;
        String AssetTypeName;
        String DisplayName;
    };

    RTTI_DEFINE_OBJECT(AssetDragData, "rtti::editor::editor::app")
}
