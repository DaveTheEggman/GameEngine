// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Pipeline::Shaders - the `shaders.pipeline` module (tooling).
//
// Source-side custom shaders + their cook (Specs/custom-shaders.md, phase 1):
//   * ShaderAsset (pipeline::Asset): a shader name, ONE .hlsl file (the inherited fileName)
//     holding both stages, and each stage's entry point (VSMain / PSMain by default).
//   * ShaderAssetBuilder (DefaultAssetBuilder): reads the file, replaces its properties block
//     with the generated set-2 declarations, and COMPILES every variant both stages declare,
//     with the engine's shader library on the include path. A failure fails the cook with
//     `File.hlsl:line: message` (the cook keeps the last good product); success writes a
//     ShaderSource carrying the expanded source, both entries, both lattices and the property
//     table every material on the shader is laid out by.
//   * ShaderImporter: an authoring helper that fills a ShaderAsset from a file path.
//
// Reads the .hlsl text through the build context's sources mount (all file access via the
// VFS). Never linked by the runtime.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Core/Log/Log.h"

export module shaders.pipeline;

import foundation.core;
import pipeline.core;
import foundation.vfs;
import foundation.shaders;
import foundation.shaders.system; // kShaderFolder: the engine's shader library under the data root
import foundation.shaders.resource;
import foundation.content;

using namespace foundation::core;
using namespace foundation::shaders;

export namespace pipeline{
    // Source asset: a shader's name, its one .hlsl file (the inherited fileName, relative to the
    // sources mount at cook time) and the entry point each stage's function is called in it.
    class ShaderAsset final : public pipeline::Asset
    {
        RTTI_OBJECT(ShaderAsset, pipeline::Asset)
    public:
        String name; // logical shader name (how materials reference it)
        String vertexEntryPoint = String(u8"VSMain");
        String fragmentEntryPoint = String(u8"PSMain");

        void Serialize(ISerializer& ar) override
        {
            pipeline::Asset::Serialize(ar); // fileName (the .hlsl)
            foundation::core::Serialize(ar, "name", name);
            foundation::core::Serialize(ar, "vertexEntryPoint", vertexEntryPoint);
            foundation::core::Serialize(ar, "fragmentEntryPoint", fragmentEntryPoint);
        }
    };

    // Cooks a ShaderAsset: expands its properties block, compiles every declared variant of both
    // stages, and writes the ShaderSource the runtime registers.
    class ShaderAssetBuilder final : public pipeline::DefaultAssetBuilder
    {
    public:
        // `dataRoot`: the engine's data root, whose shader library (`Shaders/`) is on every
        // custom shader's include path. Empty = includes resolve nowhere.
        explicit ShaderAssetBuilder(StringView dataRoot = {})
            : m_shaderLibrary(dataRoot.IsEmpty()
                                  ? String()
                                  : foundation::vfs::DataPath(dataRoot, kShaderFolder))
        {
        }

        [[nodiscard]] const TypeInfo* AssetType() const override
        {
            return &ShaderAsset::StaticType();
        }
        [[nodiscard]] const TypeInfo* ProductType() const override
        {
            return &ShaderSource::StaticType();
        }
        // v2: the product names each stage's entry point. v3: one file, compiled at cook time;
        // the product carries the expanded source, the lattices and the property table.
        [[nodiscard]] u32 Version() const override { return 3; }

        [[nodiscard]] Status Build(const pipeline::Asset& asset,
                                   pipeline::AssetBuildContext& ctx) override
        {
            const ShaderAsset& sa =
                static_cast<const ShaderAsset&>(asset); // guarded by AssetType()
            if (ctx.output == nullptr)
            {
                return Status{ErrorCode::InvalidArgument};
            }
            const StringView file = sa.fileName.View();
            if (const String taken = EngineShaderFile(sa.name.AsView()); !taken.IsEmpty())
            {
                LOG_ERROR(u8"Shaders",
                          u8"{}: the name '{}' is the engine's shader ({}): one name keys a shader "
                          u8"in the shader system and a dist's pack, so rename it - cook failed",
                          file, sa.name, taken);
                return Status{ErrorCode::InvalidArgument};
            }

            String text;
            const Status read = ReadSourceText(ctx, file, text);
            if (!read.IsOk())
            {
                return read;
            }

            // The properties block becomes the set-2 declarations; `#line` keeps the author's
            // line numbers for everything after it.
            String expanded;
            const ShaderPropertiesBlock block =
                ExpandShaderProperties(text.AsView(), file, expanded);
            for (const ShaderPropertyError& e : block.errors)
            {
                LOG_ERROR(u8"Shaders", u8"{}:{}: {} - cook failed", file, e.line, e.message);
            }
            if (!block.IsOk())
            {
                return Status{ErrorCode::InvalidArgument};
            }

            // Each stage's lattice; a flag the file tests but no stage declares would never be
            // compiled, so it fails the cook (the corpus's drift lint, over both stages).
            const ShaderFlags vertexMask = ParseStageVariantDirective(text.AsView(),
                                                                      ShaderStage::Vertex).mask;
            const ShaderFlags fragmentMask = ParseStageVariantDirective(text.AsView(),
                                                                        ShaderStage::Fragment).mask;
            Array<StringView> undeclared;
            FindUndeclaredFlagUses(text.AsView(), vertexMask | fragmentMask, undeclared);
            for (const StringView flag : undeclared)
            {
                LOG_ERROR(u8"Shaders",
                          u8"{}: tests {} but no stage declares it (add it to a "
                          u8"// variants(vertex): or // variants(fragment): line) - cook failed",
                          file, flag);
            }
            if (!undeclared.IsEmpty())
            {
                return Status{ErrorCode::InvalidArgument};
            }

            if (!CompileEveryVariant(file, expanded.AsView(), sa, vertexMask, fragmentMask))
            {
                return Status{ErrorCode::InvalidArgument};
            }

            ShaderSource source;
            source.name = sa.name;
            source.vertexSource = expanded;
            source.fragmentSource = expanded;
            source.vertexEntryPoint = sa.vertexEntryPoint;
            source.fragmentEntryPoint = sa.fragmentEntryPoint;
            source.vertexVariantMask = static_cast<u32>(vertexMask);
            source.fragmentVariantMask = static_cast<u32>(fragmentMask);
            source.SetProperties(block);
            return ctx.output->WriteObject(source);
        }

    private:
        // The engine library's file for a shader of this name (`forward` -> forward.ps.hlsl), or
        // empty when the name is free. A project shader with an engine shader's name would
        // replace it everywhere the name is looked up.
        [[nodiscard]] String EngineShaderFile(StringView name) const
        {
            if (m_shaderLibrary.IsEmpty() || name.IsEmpty())
            {
                return {};
            }
            constexpr StringView kSuffixes[] = {u8".vs.hlsl", u8".ps.hlsl", u8".cs.hlsl"};
            for (const StringView suffix : kSuffixes)
            {
                String stageFile(name);
                stageFile += suffix;
                if (FileExists(PathJoin(m_shaderLibrary.AsView(), stageFile.AsView()).AsView()))
                {
                    return stageFile;
                }
            }
            return {};
        }

        // Compiles both stages' every declared variant to SPIR-V, logging each failure as DXC
        // reports it (`File.hlsl:line:col: error: ...`). True when all compile.
        [[nodiscard]] bool CompileEveryVariant(StringView file, StringView expanded,
                                               const ShaderAsset& sa, ShaderFlags vertexMask,
                                               ShaderFlags fragmentMask) const
        {
            Compiler* compiler = nullptr;
            if (!createCompiler(CompilerDesc{}, compiler).IsOk() || compiler == nullptr)
            {
                LOG_ERROR(u8"Shaders", u8"{}: the DXC compiler is unavailable - cook failed", file);
                return false;
            }
            const StringView includePaths[] = {m_shaderLibrary.AsView()};
            const struct
            {
                ShaderStage stage;
                StringView entryPoint;
                ShaderFlags mask;
            } stages[2] = {{ShaderStage::Vertex, sa.vertexEntryPoint.AsView(), vertexMask},
                           {ShaderStage::Fragment, sa.fragmentEntryPoint.AsView(), fragmentMask}};

            bool ok = true;
            for (const auto& stage : stages)
            {
                Array<ShaderFlags> variants;
                EnumerateVariants(stage.mask, variants);
                for (const ShaderFlags flags : variants)
                {
                    Array<ShaderDefine> defines;
                    AppendDefines(flags, defines);
                    CompileOptions options{};
                    options.defines = Span<const ShaderDefine>(defines.Data(), defines.Size());
                    options.includePaths = m_shaderLibrary.IsEmpty()
                                               ? Span<const StringView>{}
                                               : Span<const StringView>(includePaths, 1);
                    options.bindingShifts = BindingShifts::Standard();
                    options.bindingShiftSets = 4;
                    options.sourceName = file;
                    CompileResult result{};
                    (void)compiler->compile(reinterpret_cast<const u8*>(expanded.Data()),
                                            expanded.Size(), stage.stage, stage.entryPoint,
                                            ShaderTarget::SPIRV, options, result);
                    if (!result.success)
                    {
                        ok = false;
                        LOG_ERROR(u8"Shaders", u8"{} ({}, variant flags {}) - cook failed: {}",
                                  file, stage.entryPoint, static_cast<u32>(flags),
                                  StringView(reinterpret_cast<const char8_t*>(
                                      result.messages != nullptr ? result.messages : "")));
                    }
                    compiler->freeResult(result);
                    if (!ok)
                    {
                        break; // one stage's first failure says what is wrong; the rest repeat it
                    }
                }
                if (!ok)
                {
                    break;
                }
            }
            compiler->Destroy();
            return ok;
        }

        String m_shaderLibrary; // <data root>/Shaders, the include path
    };

    // Authoring helper: fill a ShaderAsset from a name + its .hlsl path.
    class ShaderImporter
    {
    public:
        static void Import(StringView name, StringView file, ShaderAsset& outAsset)
        {
            outAsset.name = String(name);
            outAsset.fileName = foundation::vfs::SourcePath(file);
        }
    };

    // Registers ShaderAsset for content-DB construction + deserialization.
    inline void RegisterShaderAsset()
    {
        GlobalTypeRegistry().Register(ShaderAsset::StaticType(), TypeDomain(u8"Pipeline"));
        RegisterSerializable<ShaderAsset>();
        // The cooked PRODUCT: ReadObject constructs it by type name in cook hosts, so it
        // must be registered here (no other production site registers it).
        GlobalTypeRegistry().Register(foundation::shaders::ShaderSource::StaticType());
        RegisterSerializable<foundation::shaders::ShaderSource>();
    }

    // ShaderAsset::StaticType() is defined WITH reflected properties in ShaderAssetImpl.cpp.
}
