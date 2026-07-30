// Engine-shader cook - enumerate the built-in HLSL corpus and precompile it into a CookedShaderPack.
//
// The EXPORT-time step that retires the runtime compiler for shipped dists (docs/design/shaders.md,
// "Cook split"). For each stage file under the shader dir it: parses the variant directive, drift-
// lints (fails on a #ifdef'd-but-undeclared flag), enumerates the power set of the declared mask, and
// for every (variant x requested backend format) emits a blob into the pack - SPIR-V / DXIL via DXC,
// WGSL via the WgslTranslator. Runs on the dev/CI host only; the dist just reads the pack.

module;
#include "Core/Prelude.h"

export module draconic.shaders:pack_cook;

import draconic.core;
import :types;
import :flags;
import :variants;
import :pack;
import :compiler;
import :wgsl_cook;

using namespace draconic::core;

export namespace draconic::shaders
{
    struct ShaderCookOptions
    {
        StringView shaderDir;                    // source + #include root (e.g. Data/Shaders)
        StringView scratchDir;                   // WgslTranslator intermediates (must exist)
        Span<const CookedShaderFormat> formats;  // which backend blobs to emit
        bool validateWgsl = true;                // run tint over the WGSL
        StringView spirvTargetEnv = u8"vulkan1.3"; // for the SPIR-V (Vulkan) blobs
    };

    struct ShaderCookReport
    {
        bool success = false;
        usize filesCooked = 0;    // stage files processed
        usize variantsCooked = 0; // (variant x format) blobs emitted
        Array<String> errors;     // lint + compile diagnostics (each one line)
    };

    // Forward declarations (definitions below CookEngineShaders).
    [[nodiscard]] bool ParseStageFile(StringView fileName, ShaderStage& outStage,
                                      StringView& outStem);
    [[nodiscard]] String FormatError(StringView file, StringView msg);
    bool CookOne(Compiler& compiler, WgslTranslator& translator, StringView source,
                 ShaderStage stage, ShaderFlags flags, CookedShaderFormat format,
                 const ShaderCookOptions& opts, Span<const StringView> includePaths, StringView stem,
                 StringView fileName, CookedShaderPack& pack, ShaderCookReport& report);

    // Fill `pack` from every stage file under opts.shaderDir. On any error the pack is left partially
    // filled and success=false - the caller decides whether to ship (a dist should not).
    inline ShaderCookReport CookEngineShaders(Compiler& compiler, const ShaderCookOptions& opts,
                                              CookedShaderPack& pack)
    {
        ShaderCookReport report;

        // Enumerate the shader dir (stage files only, sorted for determinism).
        struct Collector
        {
            Array<String> files;
        } collector;
        const bool listed = ListDirectory(
            opts.shaderDir,
            [](void* ctx, StringView name, bool isDir)
            {
                if (!isDir)
                {
                    static_cast<Collector*>(ctx)->files.PushBack(String(name));
                }
            },
            &collector);
        if (!listed)
        {
            report.errors.PushBack(String(u8"could not list the shader directory"));
            return report;
        }

        const StringView includePaths[] = {opts.shaderDir};

        WgslTranslator translator(compiler, opts.scratchDir);
        translator.SetValidateWithTint(opts.validateWgsl);

        for (const String& fileName : collector.files)
        {
            ShaderStage stage = ShaderStage::Vertex;
            StringView stem;
            if (!ParseStageFile(fileName.AsView(), stage, stem))
            {
                continue; // .hlsli include or non-shader file
            }

            String pathBuf(opts.shaderDir);
            pathBuf += u8"/";
            pathBuf += fileName.AsView();
            const Result<Array<byte>> srcBytes = ReadFile(pathBuf.AsView());
            if (!srcBytes.HasValue())
            {
                report.errors.PushBack(FormatError(fileName.AsView(), u8"could not read source"));
                continue;
            }
            const Array<byte>& sb = srcBytes.Value();
            const StringView source(reinterpret_cast<const char8_t*>(sb.Data()), sb.Size());

            // Variant model: declared mask + drift-lint (used-but-undeclared is a hard error).
            const VariantDirective directive = ParseVariantDirective(source);
            Array<StringView> undeclared;
            FindUndeclaredFlagUses(source, directive.mask, undeclared);
            if (!undeclared.IsEmpty())
            {
                String msg(u8"uses undeclared variant flag(s):");
                for (usize i = 0; i < undeclared.Size(); ++i)
                {
                    msg += u8" ";
                    msg += undeclared[i];
                }
                msg += u8" (add them to the // draconic:variants directive)";
                report.errors.PushBack(FormatError(fileName.AsView(), msg.AsView()));
                continue;
            }

            // Record the declared mask so the dist runtime can canonicalize requests onto the
            // cooked lattice (dev == dist behaviour).
            pack.AddDeclaredMask(stem, stage, directive.mask);

            Array<ShaderFlags> variants;
            EnumerateVariants(directive.mask, variants);

            ++report.filesCooked;
            for (usize v = 0; v < variants.Size(); ++v)
            {
                const ShaderFlags flags = variants[v];
                for (usize f = 0; f < opts.formats.Size(); ++f)
                {
                    const CookedShaderFormat format = opts.formats[f];
                    if (CookOne(compiler, translator, source, stage, flags, format, opts,
                                Span<const StringView>(includePaths, 1), stem, fileName.AsView(),
                                pack, report))
                    {
                        ++report.variantsCooked;
                    }
                }
            }
        }

        report.success = report.errors.IsEmpty();
        return report;
    }

    // --- helpers -----------------------------------------------------------

    // "<stem>.<vs|ps|cs>.hlsl" -> (stage, stem); false for .hlsli and anything else. Matches the
    // FileShaderSourceProvider naming so cooked and dev names agree.
    [[nodiscard]] inline bool ParseStageFile(StringView fileName, ShaderStage& outStage,
                                             StringView& outStem)
    {
        StringView suffix;
        if (fileName.EndsWith(u8".vs.hlsl"))
        {
            outStage = ShaderStage::Vertex;
            suffix = u8".vs.hlsl";
        }
        else if (fileName.EndsWith(u8".ps.hlsl"))
        {
            outStage = ShaderStage::Fragment;
            suffix = u8".ps.hlsl";
        }
        else if (fileName.EndsWith(u8".cs.hlsl"))
        {
            outStage = ShaderStage::Compute;
            suffix = u8".cs.hlsl";
        }
        else
        {
            return false;
        }
        outStem = fileName.SubStr(0, fileName.Size() - suffix.Size());
        return !outStem.IsEmpty();
    }

    [[nodiscard]] inline String FormatError(StringView file, StringView msg)
    {
        String s(file);
        s += u8": ";
        s += msg;
        return s;
    }

    // Compile/translate one (variant, format) blob and Add it to the pack. Returns false + records an
    // error on failure.
    inline bool CookOne(Compiler& compiler, WgslTranslator& translator, StringView source,
                        ShaderStage stage, ShaderFlags flags, CookedShaderFormat format,
                        const ShaderCookOptions& opts, Span<const StringView> includePaths,
                        StringView stem, StringView fileName, CookedShaderPack& pack,
                        ShaderCookReport& report)
    {
        if (format == CookedShaderFormat::Wgsl)
        {
            const WgslCookResult wr = translator.Translate(source, stage, flags, includePaths);
            if (!wr.success)
            {
                report.errors.PushBack(FormatError(fileName, wr.error.AsView()));
                return false;
            }
            pack.Add(stem, stage, flags, format,
                     Span<const byte>(reinterpret_cast<const byte*>(wr.wgsl.Data()),
                                      wr.wgsl.Size()));
            return true;
        }

        // SPIR-V (Vulkan) or DXIL (DX12) via DXC.
        Array<ShaderDefine> defines;
        AppendDefines(flags, defines);
        CompileOptions co{};
        co.shaderModel = u8"6_0";
        co.optimizationLevel = 3;
        co.defines = Span<const ShaderDefine>(defines.Data(), defines.Size());
        co.includePaths = includePaths;

        ShaderTarget target = ShaderTarget::SPIRV;
        if (format == CookedShaderFormat::SpirV)
        {
            co.spirvTargetEnvironment = opts.spirvTargetEnv;
            co.bindingShifts = BindingShifts::Standard();
            co.bindingShiftSets = 4;
        }
        else // Dxil
        {
            target = ShaderTarget::DXIL;
        }

        CompileResult cr{};
        const Status st =
            compiler.compile(reinterpret_cast<const u8*>(source.Data()), source.Size(), stage,
                             u8"main", target, co, cr);
        if (st != ErrorCode::Ok || !cr.success)
        {
            report.errors.PushBack(FormatError(
                fileName, cr.messages != nullptr
                              ? StringView(reinterpret_cast<const char8_t*>(cr.messages))
                              : StringView(u8"DXC compile failed")));
            compiler.freeResult(cr);
            return false;
        }
        pack.Add(stem, stage, flags, format,
                 Span<const byte>(reinterpret_cast<const byte*>(cr.bytecode), cr.bytecodeSize));
        compiler.freeResult(cr);
        return true;
    }
}
