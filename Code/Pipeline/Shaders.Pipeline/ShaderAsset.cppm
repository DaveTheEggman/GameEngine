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
//   * CheckShaderSource (:check): what the cook proves, as data - the editor and the MCP tools
//     check a shader with it without cooking.
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
export import :check;

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

            // What the cook proves: the block expands, every flag tested is declared, and every
            // variant of both stages compiles. Each error is logged where it is, and fails it.
            ShaderCheckOptions options;
            options.vertexEntryPoint = sa.vertexEntryPoint.AsView();
            options.fragmentEntryPoint = sa.fragmentEntryPoint.AsView();
            options.shaderLibrary = m_shaderLibrary.AsView();
            options.depth = ShaderCheckDepth::EveryVariant;
            const ShaderCheck check = CheckShaderSource(text.AsView(), file, options);
            for (const ShaderDiagnostic& d : check.errors)
            {
                LOG_ERROR(u8"Shaders", u8"{} - cook failed", FormatShaderDiagnostic(d));
            }
            if (!check.IsOk())
            {
                return Status{ErrorCode::InvalidArgument};
            }

            ShaderSource source;
            source.name = sa.name;
            source.vertexSource = check.expanded;
            source.fragmentSource = check.expanded;
            source.vertexEntryPoint = sa.vertexEntryPoint;
            source.fragmentEntryPoint = sa.fragmentEntryPoint;
            source.vertexVariantMask = static_cast<u32>(check.vertexMask);
            source.fragmentVariantMask = static_cast<u32>(check.fragmentMask);
            source.SetProperties(check.block);
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
