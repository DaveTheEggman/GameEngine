// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The properties block a custom shader declares its material parameters in: HLSL's cbuffer
// packing, the parse (every type, defaults, ranges, slots), every malformed line reported with
// its line, and the expanded source compiling with the author's line numbers intact.
#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cstring>

import foundation.core;
import foundation.shaders;

using namespace foundation::core;
using namespace foundation::shaders;

namespace
{
    StringView View(const char* s) { return StringView(reinterpret_cast<const char8_t*>(s)); }

    bool Contains(StringView hay, StringView needle)
    {
        for (usize i = 0; i + needle.Size() <= hay.Size(); ++i)
        {
            if (hay.SubStr(i, needle.Size()) == needle)
            {
                return true;
            }
        }
        return false;
    }

    // The single error a source yields, or an empty one.
    ShaderPropertyError OnlyError(const char* source)
    {
        const ShaderPropertiesBlock block = ParseShaderProperties(View(source));
        REQUIRE(block.errors.Size() == 1u);
        return block.errors[0];
    }
}

TEST_CASE("properties: HLSL packing - aligned to size, never across a 16-byte row")
{
    u32 cursor = 0;
    CHECK(PackCbufferMember(cursor, 12) == 0u); // float3
    CHECK(PackCbufferMember(cursor, 4) == 12u); // a float shares the float3's row
    CHECK(PackCbufferMember(cursor, 8) == 16u); // float2
    CHECK(PackCbufferMember(cursor, 4) == 24u);
    CHECK(PackCbufferMember(cursor, 8) == 32u); // a float2 at 28 would straddle: moves to 32
    CHECK(PackCbufferMember(cursor, 16) == 48u); // float4 starts a row
    CHECK(CbufferSize(cursor) == 64u);
    u32 odd = 4;
    CHECK(PackCbufferMember(odd, 12) == 4u); // a float3 fits the rest of the row
}

TEST_CASE("properties: every type, its default, ranges and its slot")
{
    const ShaderPropertiesBlock block = ParseShaderProperties(View(
        "// the toon\n"                                   // 1
        "properties\n"                                    // 2
        "{\n"                                             // 3
        "    Color       BaseColor = (1, 0.5, 0.25);\n"   // 4: alpha left out
        "    float       Bands     = 3 [1, 8];\n"         // 5
        "    float3      Light     = (0, 1, 0);\n"        // 6
        "    float       Rim;      // a comment\n"        // 7
        "    float2      Scroll    = (0.5, -1e-1);\n"     // 8
        "    ColorHdr    Glow;\n"                         // 9
        "    int         Steps     = 4 [0, 16];\n"        // 10
        "    Texture     AlbedoMap;\n"                    // 11
        "    TextureCube Env;\n"                          // 12
        "    Sampler     MainSampler;\n"                  // 13
        "}\n"                                             // 14
        "float4 PSMain() : SV_Target0 { return BaseColor; }\n"));
    REQUIRE(block.present);
    CHECK(block.IsOk());
    CHECK(block.firstLine == 2u);
    CHECK(block.lastLine == 14u);
    REQUIRE(block.properties.Size() == 10u);

    const ShaderProperty& base = block.properties[0];
    CHECK(base.type == ShaderPropertyType::Color);
    CHECK(base.value.y == doctest::Approx(0.5f));
    CHECK(base.value.w == 1.0f); // opaque when left out
    CHECK(base.offset == 0u);
    CHECK(base.size == 16u);
    CHECK(base.line == 4u);

    const ShaderProperty& bands = block.properties[1];
    CHECK(bands.value.x == 3.0f);
    CHECK(bands.hasRange);
    CHECK(bands.rangeMax == 8.0f);
    CHECK(bands.offset == 16u);

    CHECK(block.properties[2].offset == 20u); // float3: fits the rest of Bands' row (20..31)
    CHECK(block.properties[3].offset == 32u); // Rim, no default: zero
    CHECK(block.properties[3].value.x == 0.0f);
    CHECK(block.properties[4].offset == 36u); // float2 within its row
    CHECK(block.properties[4].value.y == doctest::Approx(-0.1f));
    CHECK(block.properties[5].type == ShaderPropertyType::ColorHdr);
    CHECK(block.properties[5].offset == 48u); // at 44 it would cross a row
    CHECK(block.properties[5].value.w == 1.0f); // black at intensity 1 by default
    CHECK(block.properties[6].offset == 64u);   // int
    CHECK(block.uniformSize == 80u);

    CHECK(block.properties[7].binding == 0u); // AlbedoMap: t0
    CHECK(block.properties[8].binding == 1u); // Env: t1
    CHECK(block.properties[9].binding == 0u); // MainSampler: s0
    CHECK(block.properties[9].size == 0u);
}

TEST_CASE("properties: a source without a block is a shader without parameters")
{
    const ShaderPropertiesBlock block =
        ParseShaderProperties(View("// properties are not declared here\nfloat4 x;\n"));
    CHECK_FALSE(block.present);
    CHECK(block.IsOk());
    String out;
    (void)ExpandShaderProperties(View("float4 x;\n"), u8"A.hlsl", out);
    CHECK(out.AsView() == View("float4 x;\n"));
}

TEST_CASE("properties: every malformed line is reported with its line")
{
    CHECK(OnlyError("properties {\n  Colour Tint;\n}\n").line == 2u);
    CHECK(Contains(OnlyError("properties {\n  Colour Tint;\n}\n").message.AsView(),
                   u8"unknown property type 'Colour'"));
    CHECK(OnlyError("properties {\n  float;\n}\n").line == 2u);                      // no name
    CHECK(OnlyError("properties {\n  float A;\n  float A;\n}\n").line == 3u);         // twice
    CHECK(OnlyError("properties {\n  float2 A = 1;\n}\n").line == 2u);                // count
    CHECK(OnlyError("properties {\n  Texture A = 1;\n}\n").line == 2u);               // resource
    CHECK(OnlyError("properties {\n  Color A [0, 1];\n}\n").line == 2u);              // range type
    CHECK(OnlyError("properties {\n  float A [2, 1];\n}\n").line == 2u);              // range order
    CHECK(OnlyError("properties {\n  float A = 1\n}\n").line == 2u);                  // no ;
    CHECK(OnlyError("properties {\n  float A; float B;\n}\n").line == 2u);            // two
    CHECK(OnlyError("properties {\n  float A = (1, 2;\n}\n").line == 2u);             // no )
    CHECK(OnlyError("\nproperties {\n  float A;\n").line == 2u);                       // no }
    CHECK(OnlyError("properties\n").line == 1u);                                       // no {
}

// The expanded source is what the compiler sees: the block becomes the set-2 declarations, and
// `#line` keeps every later line's number, so a mistake after the block is reported where its
// author sees it.
TEST_CASE("properties: the expanded source compiles, and keeps the author's line numbers")
{
    Compiler* compiler = nullptr;
    if (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr)
    {
        MESSAGE("DXC unavailable; skipping");
        return;
    }
    const auto compile = [&](const char* source, String& messages) -> bool
    {
        String expanded;
        const ShaderPropertiesBlock block =
            ExpandShaderProperties(View(source), u8"Toon.hlsl", expanded);
        REQUIRE(block.IsOk());
        CompileOptions options{};
        options.sourceName = u8"Toon.hlsl";
        CompileResult result{};
        (void)compiler->compile(reinterpret_cast<const u8*>(expanded.Data()), expanded.Size(),
                                ShaderStage::Fragment, u8"PSMain", ShaderTarget::SPIRV, options,
                                result);
        messages = String(StringView(reinterpret_cast<const char8_t*>(result.messages != nullptr
                                                                          ? result.messages
                                                                          : "")));
        const bool ok = result.success;
        compiler->freeResult(result);
        return ok;
    };

    String messages;
    CHECK(compile("properties\n"                                                     // 1
                  "{\n"                                                              // 2
                  "    Color   Tint = (1, 0, 0);\n"                                  // 3
                  "    float   Strength = 2;\n"                                      // 4
                  "    Texture AlbedoMap;\n"                                         // 5
                  "    Sampler MainSampler;\n"                                       // 6
                  "}\n"                                                              // 7
                  "float4 PSMain(float2 uv : TEXCOORD0) : SV_Target0 {\n"            // 8
                  "    return AlbedoMap.Sample(MainSampler, uv) * Tint * Strength;\n" // 9
                  "}\n",
                  messages));
    CHECK(compile("properties\n"
                  "{\n"
                  "    float Strength = 2;\n"
                  "}\n"                                       // 4
                  "float4 PSMain() : SV_Target0 {\n"          // 5
                  "    return float4(1, 1, 1, 1) * Strenght;\n" // 6: the mistake
                  "}\n",
                  messages) == false);
    CAPTURE(messages);
    CHECK(Contains(messages.AsView(), u8"Toon.hlsl:6:"));
    compiler->Destroy();
}
