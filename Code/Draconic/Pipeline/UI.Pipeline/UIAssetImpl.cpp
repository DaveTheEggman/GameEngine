// Draconic::UIEditor - reflection implementation unit: UI asset reflected surfaces.
//
// Kept OUT of the UIAsset.cppm interface (DRACONIC_REFLECT bodies make GCC emit a gcm cluster;
// see gcc-module-interface-hygiene). UIDocumentAsset (markup) and UIThemeAsset (stylesheet) gain
// their string properties here. No enums, so the reflection rides StaticType() with no registrar
// change. Reflection track P1.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module draconic.ui.pipeline;

import draconic.core;
import draconic.pipeline.core;

using namespace draconic::core;
using namespace draconic::ui;

namespace draconic::pipeline{
    DRACONIC_REFLECT(UIDocumentAsset, "rtti::editor::ui")
    {
        builder.Attribute("displayName", String(u8"UI Document"))
            .Attribute("category", String(u8"UI"))
            .Property<&UIDocumentAsset::markup>("markup")
            .PropAttribute("displayName", String(u8"Markup"));
    }

    DRACONIC_REFLECT(UIThemeAsset, "rtti::editor::ui")
    {
        builder.Attribute("displayName", String(u8"UI Theme"))
            .Attribute("category", String(u8"UI"))
            .Property<&UIThemeAsset::stylesheet>("stylesheet")
            .PropAttribute("displayName", String(u8"Stylesheet"));
    }
}
