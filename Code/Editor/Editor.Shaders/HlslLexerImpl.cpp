// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Shaders - the HLSL syntax tables + lexer registration. HLSL quirks: '#' preprocessor
// lines and char literals; block comments do not nest, no heredocs. The types include the
// properties block's own (Color, ColorHdr, Texture, Sampler), and `properties` is a keyword, so
// the block reads as the language it is part of.

module;
#include "Core/Prelude.h"

module editor.shaders;

import foundation.core;
import foundation.ui.toolkit;

using namespace foundation::core;

namespace editor
{
    namespace
    {
        namespace toolkit = foundation::ui::toolkit;

        constexpr StringView kHlslKeywords[] = {
            u8"break",          u8"case",         u8"cbuffer",      u8"centroid",
            u8"column_major",   u8"const",        u8"continue",     u8"default",
            u8"discard",        u8"do",           u8"else",         u8"false",
            u8"for",            u8"groupshared",  u8"if",           u8"in",
            u8"inline",         u8"inout",        u8"linear",       u8"nointerpolation",
            u8"noperspective",  u8"out",          u8"packoffset",   u8"precise",
            u8"properties",     u8"register",     u8"return",       u8"row_major",
            u8"sample",         u8"static",       u8"struct",       u8"switch",
            u8"tbuffer",        u8"true",         u8"typedef",      u8"uniform",
            u8"unroll",         u8"loop",         u8"branch",       u8"flatten",
            u8"volatile",       u8"while"};
        constexpr StringView kHlslTypes[] = {
            // Scalars, vectors and matrices.
            u8"void", u8"bool", u8"bool2", u8"bool3", u8"bool4",
            u8"int", u8"int2", u8"int3", u8"int4",
            u8"uint", u8"uint2", u8"uint3", u8"uint4",
            u8"half", u8"half2", u8"half3", u8"half4",
            u8"float", u8"float2", u8"float3", u8"float4",
            u8"double", u8"double2", u8"double3", u8"double4",
            u8"min16float", u8"min16float2", u8"min16float3", u8"min16float4",
            u8"float2x2", u8"float2x3", u8"float2x4", u8"float3x2", u8"float3x3", u8"float3x4",
            u8"float4x2", u8"float4x3", u8"float4x4", u8"matrix", u8"vector",
            // Resources.
            u8"Texture1D", u8"Texture2D", u8"Texture2DArray", u8"Texture2DMS", u8"Texture3D",
            u8"TextureCube", u8"TextureCubeArray", u8"RWTexture2D", u8"RWTexture2DArray",
            u8"RWTexture3D", u8"SamplerState", u8"SamplerComparisonState", u8"Buffer",
            u8"RWBuffer", u8"StructuredBuffer", u8"RWStructuredBuffer", u8"ByteAddressBuffer",
            u8"RWByteAddressBuffer", u8"ConstantBuffer",
            // The properties block's types.
            u8"Color", u8"ColorHdr", u8"Texture", u8"Sampler"};

        UniquePtr<toolkit::ICodeLexer> MakeHlslLexer(IAllocator& allocator)
        {
            toolkit::CLikeLexerSpec spec;
            spec.keywords = Span<const StringView>(kHlslKeywords, ArrayCount(kHlslKeywords));
            spec.types = Span<const StringView>(kHlslTypes, ArrayCount(kHlslTypes));
            spec.hashPreprocessorLines = true;
            spec.charLiterals = true;
            return UniquePtr<toolkit::ICodeLexer>(allocator.New<toolkit::CLikeLexer>(spec),
                                                  allocator);
        }
    }

    void RegisterHlslEditorUI(IAllocator& allocator)
    {
        auto& registry = toolkit::CodeLexerRegistry::Get();
        IAllocator* owner = &allocator;
        registry.Register(u8"hlsl", [owner] { return MakeHlslLexer(*owner); });
        registry.Register(u8"hlsli", [owner] { return MakeHlslLexer(*owner); });
    }
}
