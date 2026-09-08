// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// UIDocumentAsset (markup) + UIThemeAsset (stylesheet) reflected surfaces.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import foundation.core;
import ui.pipeline;

using namespace foundation::core;
using namespace pipeline;

TEST_CASE("reflection-p1: UIDocumentAsset and UIThemeAsset expose NO inline text (the linked file is the text)")
{
    pipeline::RegisterUIAssets();
    const TypeInfo& document = pipeline::UIDocumentAsset::StaticType();
    CHECK(PropertyCount(document) == 0u);
    CHECK(FindProperty(document, "markup") == nullptr);
    CHECK(document.dataVersion == 2u);

    const TypeInfo& theme = pipeline::UIThemeAsset::StaticType();
    CHECK(PropertyCount(theme) == 0u); // previewMarkup is page-edited, never an inspector field
    CHECK(FindProperty(theme, "stylesheet") == nullptr);
    CHECK(theme.dataVersion == 3u);
}
