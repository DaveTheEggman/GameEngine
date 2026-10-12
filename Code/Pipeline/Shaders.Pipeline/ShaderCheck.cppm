// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Shaders - the `:check` partition: a custom shader checked without cooking it.
//
// CheckShaderSource runs what the cook runs on a shader's text (the properties block expanded,
// both stages' variant lattices read, every flag the file tests declared, the variants compiled
// with DXC) and returns the errors as data, each with its file, line and column, instead of
// logging them. The cook logs them and fails; the editor's shader page puts them on the lines as
// you type; the MCP tools return them. One check, so the three never disagree about a shader.

module;
#include "Core/Prelude.h"

export module shaders.pipeline:check;

import foundation.core;
import foundation.shaders;

using namespace foundation::core;
using namespace foundation::shaders;

export namespace pipeline
{
    // One error in a shader: where (the file it is in, which may be an engine library file the
    // shader includes; 1-based line and column, 0 when unknown) and what.
    struct ShaderDiagnostic
    {
        String file;
        u32 line = 0;
        u32 column = 0;
        String message;
    };

    // Which variants a check compiles. The cook proves every one; a live check as you type
    // compiles each stage's base variant, the one every draw starts from, and leaves the flags to
    // the cook so typing stays responsive.
    enum class ShaderCheckDepth : u8
    {
        BaseVariants,
        EveryVariant,
    };

    struct ShaderCheckOptions
    {
        StringView vertexEntryPoint = u8"VSMain";
        StringView fragmentEntryPoint = u8"PSMain";
        StringView shaderLibrary; // the engine's Shaders/ folder, the include path; empty = none
        ShaderCheckDepth depth = ShaderCheckDepth::EveryVariant;
        // A compiler to reuse (a page checking on every pause in typing); null = one is created
        // and destroyed for this check.
        Compiler* compiler = nullptr;
    };

    // What a check found. `expanded`, the masks and `block` are valid whenever the properties
    // block parsed; `errors` empty means the shader compiles at the depth asked.
    struct ShaderCheck
    {
        ShaderPropertiesBlock block;
        String expanded;
        ShaderFlags vertexMask = ShaderFlags::None;
        ShaderFlags fragmentMask = ShaderFlags::None;
        Array<ShaderDiagnostic> errors;

        [[nodiscard]] bool IsOk() const noexcept { return errors.IsEmpty(); }
    };

    // `file:line: message`, `file:line:column: message` or, for the whole file, `file: message`:
    // the form the Console and the cook print and a terminal makes clickable.
    [[nodiscard]] inline String FormatShaderDiagnostic(const ShaderDiagnostic& d)
    {
        if (d.line == 0)
        {
            return Format(u8"{}: {}", d.file, d.message); // about the whole file
        }
        return d.column != 0 ? Format(u8"{}:{}:{}: {}", d.file, d.line, d.column, d.message)
                             : Format(u8"{}:{}: {}", d.file, d.line, d.message);
    }

    namespace detail
    {
        [[nodiscard]] inline Optional<usize> FindIn(StringView hay, StringView needle,
                                                    usize from = 0) noexcept
        {
            for (usize i = from; i + needle.Size() <= hay.Size(); ++i)
            {
                if (hay.SubStr(i, needle.Size()) == needle)
                {
                    return i;
                }
            }
            return {};
        }

        // The position of the last ':' in [0, end), or none.
        [[nodiscard]] inline Optional<usize> LastColon(StringView text, usize end) noexcept
        {
            for (usize i = end; i > 0; --i)
            {
                if (text[i - 1] == u8':')
                {
                    return i - 1;
                }
            }
            return {};
        }
    }

    // Reads the errors out of DXC's message text, one per `path:line:col: error: message` line
    // (or `fatal error:`); notes, warnings, the source excerpts and the caret lines are skipped.
    // The path is split off from its right, so a drive letter's colon stays in it. True when at
    // least one error was found.
    inline bool ParseCompilerDiagnostics(StringView messages, Array<ShaderDiagnostic>& out)
    {
        constexpr StringView kMarkers[] = {u8": fatal error: ", u8": error: "};
        bool found = false;
        usize lineStart = 0;
        while (lineStart < messages.Size())
        {
            usize lineEnd = lineStart;
            while (lineEnd < messages.Size() && messages[lineEnd] != u8'\n')
            {
                ++lineEnd;
            }
            StringView line = messages.SubStr(lineStart, lineEnd - lineStart);
            if (!line.IsEmpty() && line[line.Size() - 1] == u8'\r')
            {
                line = line.SubStr(0, line.Size() - 1);
            }
            lineStart = lineEnd + 1;

            for (const StringView marker : kMarkers)
            {
                const Optional<usize> at = detail::FindIn(line, marker);
                if (!at.HasValue())
                {
                    continue;
                }
                // `path:line:col` before the marker.
                const StringView where = line.SubStr(0, *at);
                const Optional<usize> colColon = detail::LastColon(where, where.Size());
                const Optional<usize> lineColon =
                    colColon.HasValue() ? detail::LastColon(where, *colColon) : Optional<usize>{};
                if (!lineColon.HasValue())
                {
                    break;
                }
                const Optional<i64> lineNo =
                    ParseInt(where.SubStr(*lineColon + 1, *colColon - *lineColon - 1));
                const Optional<i64> colNo =
                    ParseInt(where.SubStr(*colColon + 1, where.Size() - *colColon - 1));
                if (!lineNo.HasValue() || !colNo.HasValue())
                {
                    break;
                }
                ShaderDiagnostic d;
                d.file = String(where.SubStr(0, *lineColon));
                d.line = static_cast<u32>(*lineNo);
                d.column = static_cast<u32>(*colNo);
                const usize text = *at + marker.Size();
                d.message = String(line.SubStr(text, line.Size() - text));
                out.PushBack(Move(d));
                found = true;
                break;
            }
        }
        return found;
    }

    // Checks a shader's text as the cook would, saved as `file` (the name errors carry).
    [[nodiscard]] inline ShaderCheck CheckShaderSource(StringView text, StringView file,
                                                       const ShaderCheckOptions& options = {})
    {
        ShaderCheck check;
        const auto fail = [&](u32 line, StringView message)
        {
            ShaderDiagnostic d;
            d.file = String(file);
            d.line = line;
            d.message = String(message);
            check.errors.PushBack(Move(d));
        };

        // The properties block becomes the set-2 declarations; `#line` keeps the author's line
        // numbers for everything after it.
        check.block = ExpandShaderProperties(text, file, check.expanded);
        for (const ShaderPropertyError& e : check.block.errors)
        {
            fail(e.line, e.message.AsView());
        }
        if (!check.block.IsOk())
        {
            return check;
        }

        // Each stage's lattice; a flag the file tests but no stage declares would never be
        // compiled (the corpus's drift lint, over both stages).
        check.vertexMask = ParseStageVariantDirective(text, ShaderStage::Vertex).mask;
        check.fragmentMask = ParseStageVariantDirective(text, ShaderStage::Fragment).mask;
        Array<StringView> undeclared;
        FindUndeclaredFlagUses(text, check.vertexMask | check.fragmentMask, undeclared);
        for (const StringView flag : undeclared)
        {
            String message(u8"tests ");
            message += flag;
            message += u8" but no stage declares it (add it to a // variants(vertex): or "
                       u8"// variants(fragment): line)";
            fail(0, message.AsView());
        }
        if (!undeclared.IsEmpty())
        {
            return check;
        }

        Compiler* compiler = options.compiler;
        if (compiler == nullptr &&
            (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr))
        {
            fail(0, u8"the DXC compiler is unavailable");
            return check;
        }

        const StringView includePaths[] = {options.shaderLibrary};
        const struct
        {
            ShaderStage stage;
            StringView entryPoint;
            ShaderFlags mask;
        } stages[2] = {{ShaderStage::Vertex, options.vertexEntryPoint, check.vertexMask},
                       {ShaderStage::Fragment, options.fragmentEntryPoint, check.fragmentMask}};
        for (const auto& stage : stages)
        {
            Array<ShaderFlags> variants;
            if (options.depth == ShaderCheckDepth::EveryVariant)
            {
                EnumerateVariants(stage.mask, variants);
            }
            else
            {
                variants.PushBack(ShaderFlags::None);
            }
            for (const ShaderFlags flags : variants)
            {
                Array<ShaderDefine> defines;
                AppendDefines(flags, defines);
                CompileOptions compile{};
                compile.defines = Span<const ShaderDefine>(defines.Data(), defines.Size());
                compile.includePaths = options.shaderLibrary.IsEmpty()
                                           ? Span<const StringView>{}
                                           : Span<const StringView>(includePaths, 1);
                compile.bindingShifts = BindingShifts::Standard();
                compile.bindingShiftSets = 4;
                compile.sourceName = file;
                CompileResult result{};
                (void)compiler->compile(reinterpret_cast<const u8*>(check.expanded.Data()),
                                        check.expanded.Size(), stage.stage, stage.entryPoint,
                                        ShaderTarget::SPIRV, compile, result);
                if (!result.success)
                {
                    const StringView messages(reinterpret_cast<const char8_t*>(
                        result.messages != nullptr ? result.messages : ""));
                    const usize first = check.errors.Size();
                    if (!ParseCompilerDiagnostics(messages, check.errors))
                    {
                        fail(0, messages.IsEmpty() ? StringView(u8"the compile failed")
                                                   : messages);
                    }
                    // The flags the failing compile had, when it had any: an error under a flag
                    // reads differently from one in every variant. (Not the stage: DXC compiles
                    // the whole file for each, so an error anywhere fails the first stage tried.)
                    if (!defines.IsEmpty())
                    {
                        String context(u8" (under ");
                        for (usize d = 0; d < defines.Size(); ++d)
                        {
                            context += d == 0 ? StringView(u8"") : StringView(u8" ");
                            context += defines[d].name;
                        }
                        context += u8")";
                        for (usize i = first; i < check.errors.Size(); ++i)
                        {
                            check.errors[i].message += context;
                        }
                    }
                }
                compiler->freeResult(result);
                if (!check.errors.IsEmpty())
                {
                    break; // one stage's first failure says what is wrong; the rest repeat it
                }
            }
            if (!check.errors.IsEmpty())
            {
                break;
            }
        }
        if (options.compiler == nullptr)
        {
            compiler->Destroy();
        }
        return check;
    }
}
