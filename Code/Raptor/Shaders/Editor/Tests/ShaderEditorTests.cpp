// Source-side shader cook: write two .hlsl files, point a ShaderAsset at them,
// cook through ShaderAssetBuilder into an output content DB, and verify the cooked
// ShaderSource carries the name + both stages' inline HLSL.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.editor;
import raptor.shaders.resource;
import raptor.shaders.editor;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::shaders;

namespace
{
    constexpr const char8_t* kVtx = u8"float4 main(uint id : SV_VertexID) : SV_Position { return float4(0,0,0,1); }\n";
    constexpr const char8_t* kFrag = u8"float4 main() : SV_Target { return float4(1,0,0,1); }\n";

    Span<const byte> Bytes(const char8_t* s)
    {
        return Span<const byte>(reinterpret_cast<const byte*>(s), CStringLength(s));
    }

    void RemoveTree()
    {
        FileDelete(u8"raptor_shader_edit/lit.vs.hlsl");
        FileDelete(u8"raptor_shader_edit/lit.fs.hlsl");
        FileDelete(u8"raptor_shader_edit_db/lit.rasset");
        RemoveDirectory(u8"raptor_shader_edit");
        RemoveDirectory(u8"raptor_shader_edit_db");
    }
}

TEST_CASE("shader editor: cooks a ShaderAsset -> ShaderSource from two .hlsl files")
{
    GlobalTypeRegistry().Register(ShaderSource::StaticType());
    RegisterSerializable<ShaderSource>();
    RegisterShaderAsset();

    RemoveTree();

    // --- author: write the two source files ---
    {
        NativeFileSystem src(u8"raptor_shader_edit");
        REQUIRE(src.Save(u8"lit.vs.hlsl", Bytes(kVtx)).IsOk());
        REQUIRE(src.Save(u8"lit.fs.hlsl", Bytes(kFrag)).IsOk());
    }

    // --- cook: ShaderAsset -> ShaderSource in the output DB ---
    NativeFileSystem outMount(u8"raptor_shader_edit_db");
    Guid id;
    {
        raptor::content::ContentDatabase outDb(outMount);
        auto* inst = outDb.RootGroup()->CreateInstance(u8"lit", ShaderSource::StaticType());
        id = inst->Id();

        ShaderAsset asset;
        ShaderImporter::Import(u8"lit", u8"lit.vs.hlsl", u8"lit.fs.hlsl", asset);

        ShaderAssetBuilder builder;
        REQUIRE(builder.AssetType() == &ShaderAsset::StaticType());
        raptor::editor::AssetBuildContext ctx{ u8"raptor_shader_edit", inst };
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- verify: read back the cooked ShaderSource ---
    {
        raptor::content::ContentDatabase outDb(outMount);
        RefPtr<ISerializable> object = outDb.ReadObject(id);
        ShaderSource* cooked = Cast<ShaderSource>(object.Get());
        REQUIRE(cooked != nullptr);
        CHECK(cooked->name == u8"lit");
        CHECK(cooked->vertexSource == StringView(kVtx));
        CHECK(cooked->fragmentSource == StringView(kFrag));
    }

    RemoveTree();
}

TEST_CASE("shader editor: missing source file is an error, not a crash")
{
    RegisterShaderAsset();
    RemoveTree();

    NativeFileSystem outMount(u8"raptor_shader_edit_db");
    raptor::content::ContentDatabase outDb(outMount);
    auto* inst = outDb.RootGroup()->CreateInstance(u8"missing", ShaderSource::StaticType());

    ShaderAsset asset;
    ShaderImporter::Import(u8"missing", u8"does_not_exist.vs.hlsl", u8"does_not_exist.fs.hlsl", asset);

    ShaderAssetBuilder builder;
    raptor::editor::AssetBuildContext ctx{ u8"raptor_shader_edit", inst };
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    RemoveTree();
}
