// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// A custom shader checked without cooking it (CheckShaderSource), the errors as data: DXC's
// message text read into file, line and column; a compile error, a properties error and an
// undeclared flag each found at their line; an error under a variant flag found by the full check
// and named with its flag, while the live (base variant) check passes it.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.shaders;
import shaders.pipeline;

using namespace foundation::core;
using namespace foundation::shaders;
using namespace pipeline;

namespace
{
    bool HaveCompiler()
    {
        Compiler* compiler = nullptr;
        if (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr)
        {
            return false;
        }
        compiler->Destroy();
        return true;
    }

    bool Has(StringView hay, StringView needle)
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

    // Wrong only under ALPHA_TEST (line 7): the plain variant compiles.
    constexpr const char8_t* kBrokenUnderFlag =
        u8"// variants(fragment): ALPHA_TEST\n"                                          // 1
        u8"float4 VSMain(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }\n"
        u8"float4 PSMain() : SV_Target0\n"                                                // 3
        u8"{\n"                                                                           // 4
        u8"    float4 c = float4(1, 1, 1, 1);\n"                                          // 5
        u8"#ifdef ALPHA_TEST\n"                                                           // 6
        u8"    if (c.a < Cutof) discard;\n"                                               // 7
        u8"#endif\n"                                                                      // 8
        u8"    return c;\n"                                                               // 9
        u8"}\n";
}

TEST_CASE("shader check: DXC's message text is read into file, line, column and message")
{
    const StringView messages =
        u8"Lava.hlsl:12:21: error: use of undeclared identifier 'Sped'\n"
        u8"    float2 uv = input.uv * Sped;\n"
        u8"                    ^\n"
        u8"Lava.hlsl:14:5: warning: implicit truncation of vector type\n"
        u8"C:\\Game\\Shaders\\lighting.hlsli:30:1: fatal error: 'missing.hlsli' file not found\r\n"
        u8"Lava.hlsl:3:1: note: in file included from here\n";
    Array<ShaderDiagnostic> out;
    REQUIRE(ParseCompilerDiagnostics(messages, out));
    REQUIRE(out.Size() == 2u); // the warning, the note and the excerpt are not errors
    CHECK(out[0].file == u8"Lava.hlsl");
    CHECK(out[0].line == 12u);
    CHECK(out[0].column == 21u);
    CHECK(out[0].message == u8"use of undeclared identifier 'Sped'");
    CHECK(out[1].file == u8"C:\\Game\\Shaders\\lighting.hlsli"); // the drive letter stays
    CHECK(out[1].line == 30u);
    CHECK(out[1].message == u8"'missing.hlsli' file not found");

    Array<ShaderDiagnostic> none;
    CHECK_FALSE(ParseCompilerDiagnostics(u8"nothing to see\n", none));
    CHECK(none.IsEmpty());
}

TEST_CASE("shader check: a diagnostic prints as file:line:column, file:line or file")
{
    ShaderDiagnostic d;
    d.file = String(u8"Lava.hlsl");
    d.line = 12;
    d.column = 21;
    d.message = String(u8"oops");
    CHECK(FormatShaderDiagnostic(d) == u8"Lava.hlsl:12:21: oops");
    d.column = 0;
    CHECK(FormatShaderDiagnostic(d) == u8"Lava.hlsl:12: oops");
    d.line = 0;
    CHECK(FormatShaderDiagnostic(d) == u8"Lava.hlsl: oops");
}

TEST_CASE("shader check: a compile error is found at the author's line, after the block")
{
    if (!HaveCompiler())
    {
        MESSAGE("DXC unavailable; skipping");
        return;
    }
    // The block's four lines are replaced by generated declarations; the error stays on line 6.
    const StringView text =
        u8"properties\n"
        u8"{\n"
        u8"    float Strength = 2;\n"
        u8"}\n"
        u8"float4 VSMain(uint id : SV_VertexID) : SV_Position { return float4(0, 0, 0, 1); }\n"
        u8"float4 PSMain() : SV_Target0 { return float4(1, 1, 1, 1) * Strenght; }\n";
    ShaderCheckOptions options;
    options.depth = ShaderCheckDepth::BaseVariants;
    const ShaderCheck check = CheckShaderSource(text, u8"Lava.hlsl", options);
    REQUIRE(check.errors.Size() == 1u);
    CHECK(check.errors[0].file == u8"Lava.hlsl");
    CHECK(check.errors[0].line == 6u);
    CHECK(check.errors[0].column > 0u);
    CHECK(Has(check.errors[0].message.AsView(), u8"Strenght"));
    CHECK_FALSE(Has(check.errors[0].message.AsView(), u8"(under")); // the plain variant
    CHECK(check.block.properties.Size() == 1u); // the table parsed even though the code failed
}

TEST_CASE("shader check: a properties error and an undeclared flag are errors with lines")
{
    const ShaderCheck bad = CheckShaderSource(u8"properties\n{\n    Colour Tint;\n}\n", u8"a.hlsl");
    REQUIRE(bad.errors.Size() == 1u);
    CHECK(bad.errors[0].line == 3u);
    CHECK(Has(bad.errors[0].message.AsView(), u8"unknown property type 'Colour'"));

    const ShaderCheck undeclared = CheckShaderSource(
        u8"float4 PSMain() : SV_Target0 {\n#ifdef GBUFFER\n#endif\n return 1; }\n", u8"b.hlsl");
    REQUIRE(undeclared.errors.Size() == 1u);
    CHECK(undeclared.errors[0].line == 0u); // about the file's directives, not a line
    CHECK(Has(undeclared.errors[0].message.AsView(), u8"tests GBUFFER but no stage declares it"));
}

TEST_CASE("shader check: an error under a flag is the full check's, named with its flag")
{
    if (!HaveCompiler())
    {
        MESSAGE("DXC unavailable; skipping");
        return;
    }
    ShaderCheckOptions live;
    live.depth = ShaderCheckDepth::BaseVariants;
    const ShaderCheck quick = CheckShaderSource(kBrokenUnderFlag, u8"cut.hlsl", live);
    CHECK(quick.IsOk());
    CHECK(quick.fragmentMask == ShaderFlags::AlphaTest);

    const ShaderCheck full = CheckShaderSource(kBrokenUnderFlag, u8"cut.hlsl"); // every variant
    REQUIRE(full.errors.Size() == 1u);
    CHECK(full.errors[0].line == 7u);
    CHECK(Has(full.errors[0].message.AsView(), u8"Cutof"));
    CHECK(Has(full.errors[0].message.AsView(), u8"(under ALPHA_TEST)"));
}

TEST_CASE("shader check: a caller's compiler is reused and left alive")
{
    Compiler* compiler = nullptr;
    if (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr)
    {
        MESSAGE("DXC unavailable; skipping");
        return;
    }
    ShaderCheckOptions options;
    options.compiler = compiler;
    options.depth = ShaderCheckDepth::BaseVariants;
    CHECK(CheckShaderSource(kBrokenUnderFlag, u8"cut.hlsl", options).IsOk());
    CHECK(CheckShaderSource(kBrokenUnderFlag, u8"cut.hlsl", options).IsOk()); // still usable
    compiler->Destroy();
}
