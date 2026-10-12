// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// ShaderAsset's reflected surface (its name and entry-point strings).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import foundation.core;
import shaders.pipeline;

using namespace foundation::core;
using namespace pipeline;

TEST_CASE("reflection-p1: ShaderAsset exposes its name and entry points, labelled, and round-trips")
{
    pipeline::RegisterShaderAsset();
    const TypeInfo& type = pipeline::ShaderAsset::StaticType();

    CHECK(PropertyCount(type) == 3u);
    const PropertyInfo* name = FindProperty(type, "name");
    const PropertyInfo* vertex = FindProperty(type, "vertexEntryPoint");
    const PropertyInfo* fragment = FindProperty(type, "fragmentEntryPoint");
    REQUIRE(name != nullptr);
    REQUIRE(vertex != nullptr);
    REQUIRE(fragment != nullptr);
    CHECK(FindAttribute(*vertex, u8"displayName") != nullptr);
    CHECK(FindAttribute(*fragment, u8"displayName") != nullptr);

    pipeline::ShaderAsset asset;
    CHECK(asset.vertexEntryPoint.AsView() == StringView(u8"VSMain")); // the defaults
    CHECK(asset.fragmentEntryPoint.AsView() == StringView(u8"PSMain"));
    Instance inst = Instance::From(&asset);
    CHECK(SetProperty(*name, inst, Variant::From(String(u8"Lit"))).IsOk());
    CHECK(asset.name.AsView() == StringView(u8"Lit"));
    CHECK(SetProperty(*fragment, inst, Variant::From(String(u8"ToonPS"))).IsOk());
    CHECK(GetProperty(*fragment, inst).Get<String>().AsView() == StringView(u8"ToonPS"));
}
