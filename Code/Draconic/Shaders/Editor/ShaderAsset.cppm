// Draconic::ShaderEditor - the `draconic.shaders.editor` module (tooling).
//
// Source-side shader authoring + cook:
//   * ShaderAsset (editor::Asset): a shader name + the two HLSL source files
//     (vertex via the inherited fileName, fragment alongside). v1 is two-stage to
//     match ShaderSource; more stages can be added the same way.
//   * ShaderAssetBuilder (DefaultAssetBuilder): reads both .hlsl files and cooks a
//     runtime ShaderSource (name + inline per-stage HLSL) into the output DB.
//   * ShaderImporter: an authoring helper that fills a ShaderAsset from file paths.
//
// Reads the .hlsl text through a NativeFileSystem rooted at the build context's
// asset root (the same path ResolveSource joins). Never linked by the runtime.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.shaders.editor;

import draconic.core;
import draconic.editor;
import draconic.vfs;
import draconic.shaders.resource;
import draconic.content;

using namespace draconic::core;

namespace draconic::shaders
{
    // Read a whole text file (relative to `root`) into `out`. Returns NotFound if
    // the file cannot be opened, Unknown on a short/failed read.
    Status ReadTextFile(StringView root, StringView file, String& out)
    {
        draconic::vfs::NativeFileSystem fs(root);
        UniquePtr<IStream> stream = fs.Open(file, FileMode::Read);
        if (stream.Get() == nullptr) { return Status{ ErrorCode::NotFound }; }

        const i64 size = stream->Size();
        if (size < 0) { return Status{ ErrorCode::Unknown }; }
        if (size == 0) { out = String{}; return Status{}; }

        Array<byte> buf;
        buf.Resize(static_cast<usize>(size));
        const u64 read = stream->Read(buf.Data(), static_cast<u64>(size));
        if (read != static_cast<u64>(size)) { return Status{ ErrorCode::Unknown }; }

        out = String(StringView(reinterpret_cast<const utf8char*>(buf.Data()), static_cast<usize>(size)));
        return Status{};
    }
}

export namespace draconic::shaders
{
    // Source asset: a shader name + the two HLSL files (vertex = inherited fileName,
    // fragment alongside). Paths are relative to the asset root at cook time.
    class ShaderAsset final : public draconic::editor::Asset
    {
        DRACONIC_OBJECT(ShaderAsset, draconic::editor::Asset)
    public:
        String name;           // logical shader name (how materials reference it)
        String fragmentFile;   // fragment-stage HLSL file (vertex = fileName)

        void Serialize(ISerializer& ar) override
        {
            draconic::editor::Asset::Serialize(ar);   // fileName (vertex source)
            draconic::core::Serialize(ar, "name", name);
            draconic::core::Serialize(ar, "fragmentFile", fragmentFile);
        }
    };

    // Cooks a ShaderAsset -> ShaderSource (read both .hlsl files -> inline source).
    class ShaderAssetBuilder final : public draconic::editor::DefaultAssetBuilder
    {
    public:
        [[nodiscard]] const TypeInfo* AssetType() const override { return &ShaderAsset::StaticType(); }

        [[nodiscard]] Status Build(const draconic::editor::Asset& asset, draconic::editor::AssetBuildContext& ctx) override
        {
            const ShaderAsset& sa = static_cast<const ShaderAsset&>(asset);   // guarded by AssetType()
            if (ctx.output == nullptr) { return Status{ ErrorCode::InvalidArgument }; }

            ShaderSource source;
            source.name = sa.name;

            Status read = ReadTextFile(ctx.assetRoot, sa.fileName.AsView(), source.vertexSource);
            if (!read.IsOk()) { return read; }
            read = ReadTextFile(ctx.assetRoot, sa.fragmentFile.AsView(), source.fragmentSource);
            if (!read.IsOk()) { return read; }

            return ctx.output->WriteObject(source);
        }
    };

    // Authoring helper: fill a ShaderAsset from a name + the two source-file paths.
    class ShaderImporter
    {
    public:
        static void Import(StringView name, StringView vertexFile, StringView fragmentFile, ShaderAsset& outAsset)
        {
            outAsset.name = String(name);
            outAsset.fileName = String(vertexFile);
            outAsset.fragmentFile = String(fragmentFile);
        }
    };

    // Registers ShaderAsset for content-DB construction + deserialization.
    inline void RegisterShaderAsset()
    {
        GlobalTypeRegistry().Register(ShaderAsset::StaticType());
        RegisterSerializable<ShaderAsset>();
    }

    DRACONIC_DEFINE_OBJECT(ShaderAsset, "draconic::shaders")
}
