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

TEST_CASE("reflection-p1: UIDocumentAsset exposes markup and round-trips")
{
    pipeline::RegisterUIAssets();
    const TypeInfo& type = pipeline::UIDocumentAsset::StaticType();

    CHECK(PropertyCount(type) == 1u);
    const PropertyInfo* markup = FindProperty(type, "markup");
    REQUIRE(markup != nullptr);

    pipeline::UIDocumentAsset asset;
    Instance inst = Instance::From(&asset);
    CHECK(SetProperty(*markup, inst, Variant::From(String(u8"<panel/>"))).IsOk());
    CHECK(GetProperty(*markup, inst).Get<String>().AsView() == StringView(u8"<panel/>"));
    CHECK(asset.markup.AsView() == StringView(u8"<panel/>"));
}

TEST_CASE("reflection-p1: UIThemeAsset exposes stylesheet and round-trips")
{
    pipeline::RegisterUIAssets();
    const TypeInfo& type = pipeline::UIThemeAsset::StaticType();

    CHECK(PropertyCount(type) == 1u);
    const PropertyInfo* sheet = FindProperty(type, "stylesheet");
    REQUIRE(sheet != nullptr);

    pipeline::UIThemeAsset asset;
    Instance inst = Instance::From(&asset);
    CHECK(SetProperty(*sheet, inst, Variant::From(String(u8"panel{}"))).IsOk());
    CHECK(asset.stylesheet.AsView() == StringView(u8"panel{}"));
}
