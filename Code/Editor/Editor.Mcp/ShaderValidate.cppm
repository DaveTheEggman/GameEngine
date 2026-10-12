// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Editor::Mcp - :shader_validate partition
//
// shader_validate: check a custom shader's source WITHOUT saving it, through CheckShaderSource,
// the check the shader cook runs (the properties block expanded, every flag tested declared,
// every variant of both stages compiled with DXC against the engine's shader library). Returns
// the errors with file, line and column, and on success the parsed properties and each stage's
// variant flags, so the agent sees what a material on it will show.

module;
#include "Core/Prelude.h"

export module editor.mcp:shader_validate;

import foundation.core;
import foundation.json;
import foundation.mcp;
import foundation.shaders;
import shaders.pipeline;

using namespace foundation::core;
using foundation::json::JsonValue;

namespace editor::mcp::detail
{
    // The keyword a properties block writes for a type (`Color`, `float2`).
    inline StringView ShaderPropertyKeyword(foundation::shaders::ShaderPropertyType type)
    {
        using T = foundation::shaders::ShaderPropertyType;
        switch (type)
        {
        case T::Float: return u8"float";
        case T::Float2: return u8"float2";
        case T::Float3: return u8"float3";
        case T::Float4: return u8"float4";
        case T::Int: return u8"int";
        case T::Color: return u8"Color";
        case T::ColorHdr: return u8"ColorHdr";
        case T::Texture: return u8"Texture";
        case T::TextureCube: return u8"TextureCube";
        case T::Sampler: return u8"Sampler";
        }
        return u8"float";
    }

    // The flags a lattice declares, by their #define names.
    inline JsonValue ShaderFlagNames(foundation::shaders::ShaderFlags mask)
    {
        JsonValue names = JsonValue::MakeArray();
        for (const foundation::shaders::ShaderFlagName& e : foundation::shaders::kShaderFlagNames)
        {
            if (foundation::shaders::HasFlag(mask, e.flag))
            {
                names.Add(JsonValue::MakeString(String(e.define)));
            }
        }
        return names;
    }
}

export namespace editor::mcp
{
    // Registers shader_validate. Project-independent (checks in-memory source). `shaderLibrary`:
    // the engine's Shaders/ folder, the include path; empty = includes resolve nowhere.
    inline void RegisterShaderValidateTool(foundation::mcp::McpServer& server,
                                           StringView shaderLibrary)
    {
        using foundation::mcp::SchemaBuilder;
        using foundation::mcp::ToolResult;

        server.RegisterTool(
            u8"shader_validate",
            u8"CHECK a custom shader's .hlsl source without saving: the exact check the shader "
            u8"cook runs (its properties block expanded, every #ifdef flag declared on a "
            u8"// variants(vertex): or // variants(fragment): line, every variant of both stages "
            u8"compiled against the engine's shader library). Returns errors with file, line and "
            u8"column (an error under one flag says so), and on success the parsed properties "
            u8"(name, type, default, range) and each stage's variant flags. Use as the loop while "
            u8"writing a shader, BEFORE cooking it.",
            SchemaBuilder()
                .Str(u8"source", u8"the full .hlsl source text", true)
                .Str(u8"name", u8"the file name errors carry (default 'shader.hlsl')")
                .Str(u8"vertexEntryPoint", u8"the vertex stage's function (default VSMain)")
                .Str(u8"fragmentEntryPoint", u8"the pixel stage's function (default PSMain)")
                .Build(),
            foundation::mcp::ToolAnnotations::ReadOnly(),
            [library = String(shaderLibrary)](const JsonValue& args) -> ToolResult
            {
                String name = args.Get(u8"name").AsString();
                if (name.IsEmpty())
                {
                    name = String(u8"shader.hlsl");
                }
                String vertexEntry = args.Get(u8"vertexEntryPoint").AsString();
                if (vertexEntry.IsEmpty())
                {
                    vertexEntry = String(u8"VSMain");
                }
                String fragmentEntry = args.Get(u8"fragmentEntryPoint").AsString();
                if (fragmentEntry.IsEmpty())
                {
                    fragmentEntry = String(u8"PSMain");
                }
                const String source = args.Get(u8"source").AsString();

                pipeline::ShaderCheckOptions options;
                options.vertexEntryPoint = vertexEntry.AsView();
                options.fragmentEntryPoint = fragmentEntry.AsView();
                options.shaderLibrary = library.AsView();
                options.depth = pipeline::ShaderCheckDepth::EveryVariant;
                const pipeline::ShaderCheck check =
                    pipeline::CheckShaderSource(source.AsView(), name.AsView(), options);

                JsonValue errors = JsonValue::MakeArray();
                for (const pipeline::ShaderDiagnostic& d : check.errors)
                {
                    JsonValue entry = JsonValue::MakeObject();
                    entry.Set(u8"file", JsonValue::MakeString(d.file));
                    entry.Set(u8"line", JsonValue::MakeNumber(static_cast<f64>(d.line)));
                    entry.Set(u8"column", JsonValue::MakeNumber(static_cast<f64>(d.column)));
                    entry.Set(u8"message", JsonValue::MakeString(d.message));
                    errors.Add(Move(entry));
                }

                JsonValue out = JsonValue::MakeObject();
                out.Set(u8"valid", JsonValue::MakeBool(check.IsOk()));
                out.Set(u8"errors", Move(errors));
                JsonValue properties = JsonValue::MakeArray();
                for (const foundation::shaders::ShaderProperty& p : check.block.properties)
                {
                    JsonValue entry = JsonValue::MakeObject();
                    entry.Set(u8"name", JsonValue::MakeString(p.name));
                    entry.Set(u8"type",
                              JsonValue::MakeString(String(detail::ShaderPropertyKeyword(p.type))));
                    JsonValue value = JsonValue::MakeArray();
                    value.Add(JsonValue::MakeNumber(static_cast<f64>(p.value.x)));
                    value.Add(JsonValue::MakeNumber(static_cast<f64>(p.value.y)));
                    value.Add(JsonValue::MakeNumber(static_cast<f64>(p.value.z)));
                    value.Add(JsonValue::MakeNumber(static_cast<f64>(p.value.w)));
                    entry.Set(u8"default", Move(value));
                    if (p.hasRange)
                    {
                        JsonValue range = JsonValue::MakeArray();
                        range.Add(JsonValue::MakeNumber(static_cast<f64>(p.rangeMin)));
                        range.Add(JsonValue::MakeNumber(static_cast<f64>(p.rangeMax)));
                        entry.Set(u8"range", Move(range));
                    }
                    properties.Add(Move(entry));
                }
                out.Set(u8"properties", Move(properties));
                out.Set(u8"vertexVariants", detail::ShaderFlagNames(check.vertexMask));
                out.Set(u8"fragmentVariants", detail::ShaderFlagNames(check.fragmentMask));
                return out;
            });
    }
}
