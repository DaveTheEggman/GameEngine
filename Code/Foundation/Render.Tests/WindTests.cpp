// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// WIND: which materials select the sway variant (a WindStrength default above zero), the flag's
// name in the variant table, and the PBR template's Wind lanes sitting in the cbuffer's spare
// slots (the block stays 64 bytes, so a material without them is byte-identical).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.shaders;
import foundation.materials;
import foundation.render;

using namespace foundation::core;
using namespace foundation::render;
namespace materials = foundation::materials;
namespace shaders = foundation::shaders;

TEST_CASE("wind: a material opts in through a WindStrength default above zero")
{
    CHECK(!MeshRenderer::MaterialWantsWind(nullptr));

    RefPtr<materials::Material> plain = materials::CreatePBR(u8"plain");
    CHECK(!MeshRenderer::MaterialWantsWind(plain.Get())); // declared, default 0: no sway

    RefPtr<materials::Material> card = materials::CreatePBR(u8"card");
    card->SetDefaultFloat(u8"WindStrength", 0.2f);
    CHECK(MeshRenderer::MaterialWantsWind(card.Get()));
    card->SetDefaultFloat(u8"WindStrength", 0.0f);
    CHECK(!MeshRenderer::MaterialWantsWind(card.Get()));

    // A material without the property (an older cooked source, an unlit) never sways.
    RefPtr<materials::Material> unlit = materials::CreateUnlit(u8"unlit");
    CHECK(!MeshRenderer::MaterialWantsWind(unlit.Get()));
}

TEST_CASE("wind: the PBR template's Wind lanes are the cbuffer's spare slots; the block stays 64 bytes")
{
    RefPtr<materials::Material> pbr = materials::CreatePBR(u8"pbr");
    const materials::MaterialPropertyDef* strength = pbr->FindProperty(u8"WindStrength");
    const materials::MaterialPropertyDef* speed = pbr->FindProperty(u8"WindSpeed");
    const materials::MaterialPropertyDef* height = pbr->FindProperty(u8"WindHeight");
    REQUIRE(strength != nullptr);
    REQUIRE(speed != nullptr);
    REQUIRE(height != nullptr);
    CHECK(strength->offset == 24u); // the lanes after Roughness (20)
    CHECK(speed->offset == 28u);
    CHECK(height->offset == 60u); // after AlphaCutoff (56)
    CHECK(pbr->FindProperty(u8"EmissiveColor")->offset == 32u); // unmoved
    CHECK(pbr->FindProperty(u8"AlphaCutoff")->offset == 56u);
    CHECK(pbr->UniformDataSize() == 64u);

    // The flag names its #define, so a stage's `variants:` line can declare it.
    CHECK(shaders::FlagFromName(u8"WIND") == shaders::ShaderFlags::Wind);
}
