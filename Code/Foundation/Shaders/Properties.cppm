// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Shader properties - the `properties { ... }` block a custom shader declares its material
// parameters in, and the HLSL the engine generates from it (Specs/custom-shaders.md, decided
// 2026-10-12). The block is the ONE declaration of a shader's material layout, in both kinds of
// custom shader: the engine replaces it with the set-2 declarations (the `Material` cbuffer, the
// textures and the samplers), so the layout a material is cooked with and the one the shader
// reads cannot disagree.
//
//     properties
//     {
//         Color   BaseColor = (1, 1, 1, 1);
//         float   Bands     = 3   [1, 8];
//         Texture AlbedoMap;
//         Sampler MainSampler;
//     }
//
// One declaration per line: a type, a name, an optional default and, for a float or an int, an
// optional [min, max] range (the editor's slider). The cbuffer follows HLSL's packing rules,
// through PackCbufferMember, which MaterialBuilder uses too.

module;
#include "Core/Prelude.h"

export module foundation.shaders:properties;

import foundation.core;

using namespace foundation::core;

export namespace foundation::shaders
{
    enum class ShaderPropertyType : u8
    {
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Color,    // authored sRGB rgba; the shader reads it linear (a float4)
        ColorHdr, // authored sRGB rgb, linear intensity in w (a float4)
        Texture,  // Texture2D
        TextureCube,
        Sampler,
    };

    // HLSL's cbuffer packing, the one rule every material block is laid out by: a member is
    // aligned to its own size and never crosses a 16-byte row (a float3 then a float share a row;
    // a float2 at 12 moves to 16). Returns the member's offset and advances `cursor` past it.
    [[nodiscard]] inline u32 PackCbufferMember(u32& cursor, u32 size) noexcept
    {
        u32 offset = cursor;
        if (size > 0u && offset / 16u != (offset + size - 1u) / 16u)
        {
            offset = (offset + 15u) & ~15u;
        }
        cursor = offset + size;
        return offset;
    }

    // A cbuffer's size: its members' extent rounded up to a whole row.
    [[nodiscard]] inline u32 CbufferSize(u32 cursor) noexcept { return (cursor + 15u) & ~15u; }

    struct ShaderProperty
    {
        String name;
        ShaderPropertyType type = ShaderPropertyType::Float;
        Float4 value{0.0f, 0.0f, 0.0f, 0.0f}; // the default (an int's in x)
        bool hasRange = false;
        f32 rangeMin = 0.0f;
        f32 rangeMax = 0.0f;
        u32 line = 0;    // where it is declared (1-based)
        u32 offset = 0;  // a uniform's byte offset in the cbuffer
        u32 size = 0;    // a uniform's byte size; 0 for a texture or sampler
        u32 binding = 0; // a texture's tN or a sampler's sN
    };

    struct ShaderPropertyError
    {
        u32 line = 0; // 1-based
        String message;
    };

    // What ParseShaderProperties found. `present` false = the source declares no block (a valid
    // shader with no material parameters).
    struct ShaderPropertiesBlock
    {
        bool present = false;
        usize begin = 0;    // byte offset of the `properties` line's start
        usize end = 0;      // byte offset just past the closing brace's line
        u32 firstLine = 0;  // the `properties` line (1-based)
        u32 lastLine = 0;   // the closing brace's line
        Array<ShaderProperty> properties; // in declaration order
        Array<ShaderPropertyError> errors;
        u32 uniformSize = 0; // the cbuffer's size (0 = no uniforms)

        [[nodiscard]] bool IsOk() const noexcept { return errors.IsEmpty(); }
    };

    namespace detail
    {
        [[nodiscard]] inline bool IsSpace(char8_t c) noexcept
        {
            return c == u8' ' || c == u8'\t' || c == u8'\r';
        }
        [[nodiscard]] inline bool IsIdentStart(char8_t c) noexcept
        {
            return (c >= u8'A' && c <= u8'Z') || (c >= u8'a' && c <= u8'z') || c == u8'_';
        }
        [[nodiscard]] inline bool IsIdent(char8_t c) noexcept
        {
            return IsIdentStart(c) || (c >= u8'0' && c <= u8'9');
        }

        // A cursor over one line of the block.
        struct LineReader
        {
            StringView text;
            usize at = 0;

            void SkipSpace()
            {
                while (at < text.Size() && IsSpace(text.Data()[at]))
                {
                    ++at;
                }
            }
            [[nodiscard]] bool AtEnd() const noexcept { return at >= text.Size(); }
            [[nodiscard]] char8_t Peek() const noexcept
            {
                return AtEnd() ? u8'\0' : text.Data()[at];
            }
            bool Take(char8_t c)
            {
                SkipSpace();
                if (Peek() == c)
                {
                    ++at;
                    return true;
                }
                return false;
            }
            [[nodiscard]] StringView Identifier()
            {
                SkipSpace();
                const usize start = at;
                if (!AtEnd() && IsIdentStart(Peek()))
                {
                    while (!AtEnd() && IsIdent(Peek()))
                    {
                        ++at;
                    }
                }
                return text.SubStr(start, at - start);
            }
            // A decimal number (sign, digits, optional fraction and exponent).
            bool Number(f32& out)
            {
                SkipSpace();
                const usize start = at;
                if (Peek() == u8'-' || Peek() == u8'+')
                {
                    ++at;
                }
                bool digits = false;
                while (!AtEnd() && ((Peek() >= u8'0' && Peek() <= u8'9') || Peek() == u8'.'))
                {
                    digits = digits || Peek() != u8'.';
                    ++at;
                }
                if (digits && (Peek() == u8'e' || Peek() == u8'E'))
                {
                    ++at;
                    if (Peek() == u8'-' || Peek() == u8'+')
                    {
                        ++at;
                    }
                    while (!AtEnd() && Peek() >= u8'0' && Peek() <= u8'9')
                    {
                        ++at;
                    }
                }
                if (!digits)
                {
                    at = start;
                    return false;
                }
                const Optional<f64> parsed = ParseFloat(text.SubStr(start, at - start));
                if (!parsed.HasValue())
                {
                    at = start;
                    return false;
                }
                out = static_cast<f32>(parsed.Value());
                return true;
            }
        };

        struct TypeInfoRow
        {
            StringView keyword;
            ShaderPropertyType type;
            u32 components; // 0 = a resource (no default)
            StringView hlsl;
        };
        inline constexpr TypeInfoRow kTypes[] = {
            {u8"float", ShaderPropertyType::Float, 1, u8"float"},
            {u8"float2", ShaderPropertyType::Float2, 2, u8"float2"},
            {u8"float3", ShaderPropertyType::Float3, 3, u8"float3"},
            {u8"float4", ShaderPropertyType::Float4, 4, u8"float4"},
            {u8"int", ShaderPropertyType::Int, 1, u8"int"},
            {u8"Color", ShaderPropertyType::Color, 4, u8"float4"},
            {u8"ColorHdr", ShaderPropertyType::ColorHdr, 4, u8"float4"},
            {u8"Texture", ShaderPropertyType::Texture, 0, u8"Texture2D"},
            {u8"TextureCube", ShaderPropertyType::TextureCube, 0, u8"TextureCube"},
            {u8"Sampler", ShaderPropertyType::Sampler, 0, u8"SamplerState"},
        };

        [[nodiscard]] inline const TypeInfoRow* FindType(StringView keyword) noexcept
        {
            for (const TypeInfoRow& row : kTypes)
            {
                if (row.keyword == keyword)
                {
                    return &row;
                }
            }
            return nullptr;
        }

        [[nodiscard]] inline const TypeInfoRow& RowOf(ShaderPropertyType type) noexcept
        {
            for (const TypeInfoRow& row : kTypes)
            {
                if (row.type == type)
                {
                    return row;
                }
            }
            return kTypes[0];
        }

        inline void AddError(ShaderPropertiesBlock& block, u32 line, StringView message)
        {
            block.errors.PushBack(ShaderPropertyError{line, String(message)});
        }

        // The `properties` line: a line whose first token is the word `properties`, outside a
        // `//` comment. Returns its start offset, or npos-like `source.Size()` when absent.
        [[nodiscard]] inline usize FindBlockStart(StringView source, u32& outLine) noexcept
        {
            constexpr StringView kKeyword = u8"properties";
            usize lineStart = 0;
            u32 line = 1;
            while (lineStart < source.Size())
            {
                usize lineEnd = lineStart;
                while (lineEnd < source.Size() && source.Data()[lineEnd] != u8'\n')
                {
                    ++lineEnd;
                }
                usize k = lineStart;
                while (k < lineEnd && IsSpace(source.Data()[k]))
                {
                    ++k;
                }
                const usize after = k + kKeyword.Size();
                if (after <= lineEnd && source.SubStr(k, kKeyword.Size()) == kKeyword &&
                    (after == lineEnd || !IsIdent(source.Data()[after])))
                {
                    outLine = line;
                    return lineStart;
                }
                lineStart = lineEnd + 1;
                ++line;
            }
            return source.Size();
        }

        // One declaration line (already stripped of a trailing `//` comment).
        inline void ParseDeclaration(ShaderPropertiesBlock& block, StringView text, u32 line)
        {
            LineReader r{text};
            const StringView typeWord = r.Identifier();
            const TypeInfoRow* row = FindType(typeWord);
            if (row == nullptr)
            {
                String message(u8"unknown property type '");
                message += typeWord;
                message += u8"' (float, float2, float3, float4, int, Color, ColorHdr, Texture, "
                           u8"TextureCube, Sampler)";
                AddError(block, line, message.AsView());
                return;
            }
            const StringView name = r.Identifier();
            if (name.IsEmpty())
            {
                AddError(block, line, u8"a property needs a name after its type");
                return;
            }
            for (const ShaderProperty& existing : block.properties)
            {
                if (existing.name.AsView() == name)
                {
                    String message(u8"'");
                    message += name;
                    message += u8"' is declared twice";
                    AddError(block, line, message.AsView());
                    return;
                }
            }

            ShaderProperty p;
            p.name = String(name);
            p.type = row->type;
            p.line = line;
            // Defaults when none is written: white for a colour, black at intensity 1 for an HDR
            // colour (no glow), zero otherwise.
            if (p.type == ShaderPropertyType::Color)
            {
                p.value = Float4{1.0f, 1.0f, 1.0f, 1.0f};
            }
            else if (p.type == ShaderPropertyType::ColorHdr)
            {
                p.value = Float4{0.0f, 0.0f, 0.0f, 1.0f};
            }

            if (r.Take(u8'='))
            {
                if (row->components == 0)
                {
                    AddError(block, line, u8"a texture or sampler takes no default");
                    return;
                }
                f32 values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                u32 count = 0;
                if (r.Take(u8'('))
                {
                    do
                    {
                        if (count == 4 || !r.Number(values[count]))
                        {
                            AddError(block, line, u8"a default is a number or (a, b, ...)");
                            return;
                        }
                        ++count;
                    } while (r.Take(u8','));
                    if (!r.Take(u8')'))
                    {
                        AddError(block, line, u8"a default's ( has no )");
                        return;
                    }
                }
                else if (r.Number(values[0]))
                {
                    count = 1;
                }
                else
                {
                    AddError(block, line, u8"a default is a number or (a, b, ...)");
                    return;
                }
                // A colour may leave its alpha out (opaque).
                const bool colour = p.type == ShaderPropertyType::Color ||
                                    p.type == ShaderPropertyType::ColorHdr;
                const bool colourWithoutAlpha = colour && count == 3;
                if (count != row->components && !colourWithoutAlpha)
                {
                    String message(u8"'");
                    message += name;
                    message += u8"' is a ";
                    message += row->keyword;
                    message += u8": its default needs ";
                    message += Format(u8"{}", row->components);
                    message += row->components == 1 ? u8" value" : u8" values";
                    AddError(block, line, message.AsView());
                    return;
                }
                p.value = Float4{values[0], values[1], values[2],
                                 colourWithoutAlpha ? 1.0f : values[3]};
            }

            if (r.Take(u8'['))
            {
                if (p.type != ShaderPropertyType::Float && p.type != ShaderPropertyType::Int)
                {
                    AddError(block, line, u8"a [min, max] range is for a float or an int");
                    return;
                }
                if (!r.Number(p.rangeMin) || !r.Take(u8',') || !r.Number(p.rangeMax) ||
                    !r.Take(u8']'))
                {
                    AddError(block, line, u8"a range is [min, max]");
                    return;
                }
                if (p.rangeMin > p.rangeMax)
                {
                    AddError(block, line, u8"a range's min is above its max");
                    return;
                }
                p.hasRange = true;
            }

            if (!r.Take(u8';'))
            {
                AddError(block, line, u8"a declaration ends with ;");
                return;
            }
            r.SkipSpace();
            if (!r.AtEnd())
            {
                AddError(block, line, u8"one declaration per line");
                return;
            }
            block.properties.PushBack(Move(p));
        }
    }

    // Find and parse the source's properties block. Errors carry their line; the block's place
    // (begin/end, first/last line) is what ExpandShaderProperties replaces.
    [[nodiscard]] inline ShaderPropertiesBlock ParseShaderProperties(StringView source)
    {
        ShaderPropertiesBlock block;
        u32 line = 0;
        const usize start = detail::FindBlockStart(source, line);
        if (start >= source.Size())
        {
            return block; // no block: a shader without material parameters
        }
        block.present = true;
        block.begin = start;
        block.firstLine = line;

        // The opening brace: after the keyword on its line, or alone on a following line.
        usize at = start;
        while (at < source.Size() && source.Data()[at] != u8'{')
        {
            if (source.Data()[at] == u8'\n')
            {
                ++line;
            }
            ++at;
        }
        if (at >= source.Size())
        {
            detail::AddError(block, block.firstLine, u8"properties needs a { ... } block");
            block.end = source.Size();
            block.lastLine = line;
            return block;
        }
        ++at; // past {

        // Declarations, a line at a time, to the closing brace.
        bool closed = false;
        while (at < source.Size())
        {
            usize lineEnd = at;
            while (lineEnd < source.Size() && source.Data()[lineEnd] != u8'\n')
            {
                ++lineEnd;
            }
            StringView text = source.SubStr(at, lineEnd - at);
            // A trailing `//` comment is not part of the declaration.
            for (usize i = 0; i + 1 < text.Size(); ++i)
            {
                if (text.Data()[i] == u8'/' && text.Data()[i + 1] == u8'/')
                {
                    text = text.SubStr(0, i);
                    break;
                }
            }
            // Trim.
            usize b = 0;
            usize e = text.Size();
            while (b < e && detail::IsSpace(text.Data()[b]))
            {
                ++b;
            }
            while (e > b && detail::IsSpace(text.Data()[e - 1]))
            {
                --e;
            }
            text = text.SubStr(b, e - b);
            if (!text.IsEmpty() && text.Data()[0] == u8'}')
            {
                closed = true;
                block.lastLine = line;
                block.end = lineEnd < source.Size() ? lineEnd + 1 : lineEnd;
                if (text.Size() > 1)
                {
                    detail::AddError(block, line, u8"nothing may follow the block's }");
                }
                break;
            }
            if (!text.IsEmpty())
            {
                detail::ParseDeclaration(block, text, line);
            }
            if (lineEnd >= source.Size())
            {
                break;
            }
            at = lineEnd + 1;
            ++line;
        }
        if (!closed)
        {
            detail::AddError(block, block.firstLine, u8"the properties block has no closing }");
            block.end = source.Size();
            block.lastLine = line;
            return block;
        }

        // Lay out: uniforms by HLSL's packing, textures t0.., samplers s0.. in declaration order
        // (what MaterialSystem assigns a material with this property list).
        u32 cursor = 0;
        u32 textures = 0;
        u32 samplers = 0;
        bool anyUniform = false;
        for (ShaderProperty& p : block.properties)
        {
            switch (p.type)
            {
            case ShaderPropertyType::Texture:
            case ShaderPropertyType::TextureCube:
                p.binding = textures++;
                break;
            case ShaderPropertyType::Sampler:
                p.binding = samplers++;
                break;
            default:
                p.size = detail::RowOf(p.type).components * 4u;
                p.offset = PackCbufferMember(cursor, p.size);
                anyUniform = true;
                break;
            }
        }
        block.uniformSize = anyUniform ? CbufferSize(cursor) : 0u;
        return block;
    }

    // The set-2 declarations a block stands for: the `Material` cbuffer (members in order, which
    // HLSL packs exactly as PackCbufferMember did), then each texture and sampler at its slot.
    [[nodiscard]] inline String GenerateMaterialDeclarations(const ShaderPropertiesBlock& block)
    {
        String out;
        if (block.uniformSize > 0u)
        {
            out += u8"cbuffer Material : register(b0, space2) {\n";
            for (const ShaderProperty& p : block.properties)
            {
                if (p.size == 0u)
                {
                    continue;
                }
                out += u8"    ";
                out += detail::RowOf(p.type).hlsl;
                out += u8" ";
                out += p.name;
                out += u8"; // offset ";
                out += Format(u8"{}", p.offset);
                out += u8"\n";
            }
            out += u8"};\n";
        }
        for (const ShaderProperty& p : block.properties)
        {
            if (p.size != 0u)
            {
                continue;
            }
            const bool sampler = p.type == ShaderPropertyType::Sampler;
            out += detail::RowOf(p.type).hlsl;
            out += u8" ";
            out += p.name;
            out += sampler ? u8" : register(s" : u8" : register(t";
            out += Format(u8"{}", p.binding);
            out += u8", space2);\n";
        }
        return out;
    }

    // The source as the compiler sees it: the properties block replaced by its declarations, then
    // `#line` so every line after it keeps the number (and file name) its author sees. A source
    // with no block is returned as it is. The parse is returned for its table and its errors.
    [[nodiscard]] inline ShaderPropertiesBlock ExpandShaderProperties(StringView source,
                                                                      StringView fileName,
                                                                      String& out)
    {
        ShaderPropertiesBlock block = ParseShaderProperties(source);
        if (!block.present)
        {
            out = String(source);
            return block;
        }
        out = String(source.SubStr(0, block.begin));
        out += GenerateMaterialDeclarations(block);
        out += u8"#line ";
        out += Format(u8"{}", block.lastLine + 1u);
        if (!fileName.IsEmpty())
        {
            out += u8" \"";
            out += fileName;
            out += u8"\"";
        }
        out += u8"\n";
        out += source.SubStr(block.end, source.Size() - block.end);
        return block;
    }
}
