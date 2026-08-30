// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// ShaderAsset's reflected surface (name + fragmentFile strings).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import foundation.core;
import shaders.pipeline;

using namespace foundation::core;
using namespace pipeline;

TEST_CASE("reflection-p1: ShaderAsset exposes name + fragmentFile with labels, and round-trips")
{
    pipeline::RegisterShaderAsset();
    const TypeInfo& type = pipeline::ShaderAsset::StaticType();

    CHECK(PropertyCount(type) == 2u);
    const PropertyInfo* name = FindProperty(type, "name");
    const PropertyInfo* frag = FindProperty(type, "fragmentFile");
    REQUIRE(name != nullptr);
    REQUIRE(frag != nullptr);
    CHECK(FindAttribute(*frag, u8"displayName") != nullptr);

    pipeline::ShaderAsset asset;
    Instance inst = Instance::From(&asset);
    CHECK(SetProperty(*name, inst, Variant::From(String(u8"Lit"))).IsOk());
    CHECK(asset.name.AsView() == StringView(u8"Lit"));
    CHECK(SetProperty(*frag, inst, Variant::From(String(u8"lit.frag.hlsl"))).IsOk());
    CHECK(GetProperty(*frag, inst).Get<String>().AsView() == StringView(u8"lit.frag.hlsl"));
}
