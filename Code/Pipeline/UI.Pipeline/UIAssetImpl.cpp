// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::UI - reflection implementation unit: UI asset reflected surfaces.
//
// Kept OUT of the UIAsset.cppm interface (REFLECT_MEMBERS bodies make GCC emit a gcm cluster;
// see gcc-module-interface-hygiene). UIDocumentAsset (markup) and UIThemeAsset (stylesheet) gain
// their string properties here. No enums, so the reflection rides StaticType() with no registrar
// change.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module ui.pipeline;

import foundation.core;
import pipeline.core;

using namespace foundation::core;
using namespace foundation::ui;

namespace pipeline{
    REFLECT_MEMBERS(UIDocumentAsset, "rtti::pipeline::ui")
    {
        builder.Attribute("displayName", String(u8"UI Document"))
            .Attribute("category", String(u8"UI"))
            .Property<&UIDocumentAsset::markup>("markup")
            .PropAttribute("displayName", String(u8"Markup"));
    }

    REFLECT_MEMBERS(UIThemeAsset, "rtti::pipeline::ui")
    {
        builder.Attribute("displayName", String(u8"UI Theme"))
            .Attribute("category", String(u8"UI"))
            .DataVersion(2) // v2 = editor-only previewMarkup (see UIThemeAsset::Serialize)
            .Property<&UIThemeAsset::stylesheet>("stylesheet")
            .PropAttribute("displayName", String(u8"Stylesheet"));
        // previewMarkup is editor-only scaffolding (page-edited, not an inspector field) - persisted
        // via Serialize, deliberately NOT reflected as a Property.
    }
}
