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

TEST_CASE("wind: the snapshot carries its scene's clock; a reset clears it (the frame clock stands in)")
{
    ExtractedScene scene{DefaultAllocator()};
    CHECK(!scene.HasTime());
    scene.SetTime(12.5f, 12.4f);
    CHECK(scene.HasTime());
    CHECK(scene.TimeSeconds() == 12.5f);
    CHECK(scene.PrevTimeSeconds() == 12.4f);
    scene.Reset();
    CHECK(!scene.HasTime());
    CHECK(scene.TimeSeconds() == 0.0f);
}

// The depth-like passes (the depth prepass, the shadow maps, the pick): the built-in programs, or
// a material's own when its shader has them, which then bind the material's set 2 they were
// generated for.
TEST_CASE("depth passes: a material's own depth and pick programs replace the built-ins")
{
    RenderRecordContext ctx{};
    RefPtr<materials::Material> plain = materials::CreatePBR(u8"plain");
    const MeshRenderer::DepthLikeChoice builtIn =
        MeshRenderer::DepthLikeFor(ctx, plain.Get(), /*instanced*/ false, /*pick*/ false);
    CHECK(builtIn.config.shaderName == u8"shadow_depth");
    CHECK_FALSE(builtIn.materialSet); // opaque, still: no set 2
    CHECK(builtIn.config.depthOnly);

    RefPtr<materials::Material> waves = materials::CreatePBR(u8"waves");
    waves->depthShaderName = String(u8"waves.depth");
    waves->pickShaderName = String(u8"waves.pick");
    waves->pipeline.cullMode = materials::CullModeConfig::None;
    const MeshRenderer::DepthLikeChoice depth =
        MeshRenderer::DepthLikeFor(ctx, waves.Get(), /*instanced*/ true, /*pick*/ false);
    CHECK(depth.config.shaderName == u8"waves.depth");
    CHECK(depth.materialSet);
    CHECK(shaders::HasFlag(depth.config.shaderFlags, shaders::ShaderFlags::Instanced));
    CHECK(depth.config.depthOnly); // opaque: its vertex stage alone
    const MeshRenderer::DepthLikeChoice pick =
        MeshRenderer::DepthLikeFor(ctx, waves.Get(), /*instanced*/ false, /*pick*/ true);
    CHECK(pick.config.shaderName == u8"waves.pick");
    CHECK(pick.config.cullMode == materials::CullModeConfig::None); // as the material draws

    // Masked: the cut-out's fragment runs, over the material's set 2.
    waves->pipeline.blendMode = materials::BlendMode::Masked;
    const MeshRenderer::DepthLikeChoice masked =
        MeshRenderer::DepthLikeFor(ctx, waves.Get(), false, false);
    CHECK_FALSE(masked.config.depthOnly);
    CHECK(shaders::HasFlag(masked.config.shaderFlags, shaders::ShaderFlags::AlphaTest));

    // No material: the built-in.
    CHECK(MeshRenderer::DepthLikeFor(ctx, nullptr, false, true).config.shaderName == u8"pick_ids");
}
