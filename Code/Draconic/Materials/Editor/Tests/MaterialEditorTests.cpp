// Source-side material cook: author a Material in code, capture it into a MaterialAsset
// referencing a shader id, cook through MaterialAssetBuilder into an output content DB,
// and verify the cooked MaterialSource carries the shader id, declared properties, and
// default uniforms. No GPU/DXC - pure authoring data.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.editor;
import draconic.materials;
import draconic.materials.resource;
import draconic.materials.editor;

using namespace draconic::core;
using namespace draconic::vfs;
using namespace draconic::materials;

namespace
{
    void RemoveTree()
    {
        FileDelete(u8"draconic_mat_edit_db/lit.rasset");
        RemoveDirectory(u8"draconic_mat_edit_db");
    }
}

TEST_CASE("material editor: cooks a MaterialAsset -> MaterialSource")
{
    GlobalTypeRegistry().Register(MaterialSource::StaticType());
    RegisterSerializable<MaterialSource>();
    RegisterMaterialAsset();

    RemoveTree();

    Guid shaderId{ 0x1122334455667788ull, 0x99aabbccddeeff00ull };
    NativeFileSystem outMount(u8"draconic_mat_edit_db");
    Guid id;

    // --- cook: author a material, import into an asset, build into the output DB ---
    {
        draconic::content::ContentDatabase outDb(outMount, draconic::core::BinarySerializerFactory(), u8".rasset");
        auto* inst = outDb.RootGroup()->CreateInstance(u8"lit", MaterialSource::StaticType());
        id = inst->Id();

        RefPtr<Material> authored = MaterialBuilder(u8"litMat")
            .Shader(u8"lit")
            .Transparent()
            .Color(u8"tint", Float4{ 0.1f, 0.2f, 0.3f, 1.0f })
            .Float(u8"metallic", 0.7f)
            .Texture(u8"albedoMap")
            .Build();

        MaterialAsset asset;
        MaterialImporter::Import(*authored, shaderId, asset);

        MaterialAssetBuilder builder;
        REQUIRE(builder.AssetType() == &MaterialAsset::StaticType());
        draconic::editor::AssetBuildContext ctx;
        ctx.output = inst;
        REQUIRE(builder.Build(asset, ctx).IsOk());
    }

    // --- verify: read back the cooked MaterialSource ---
    {
        draconic::content::ContentDatabase outDb(outMount, draconic::core::BinarySerializerFactory(), u8".rasset");
        RefPtr<ISerializable> object = outDb.ReadObject(id);
        MaterialSource* cooked = Cast<MaterialSource>(object.Get());
        REQUIRE(cooked != nullptr);
        CHECK(cooked->name == u8"litMat");
        CHECK(cooked->shaderId == shaderId);
        CHECK(cooked->blendMode == static_cast<u8>(BlendMode::AlphaBlend));   // .Transparent()
        CHECK(cooked->depthMode == static_cast<u8>(DepthMode::ReadOnly));
        REQUIRE(cooked->propNames.Size() == 3);
        CHECK(cooked->propNames[0] == u8"tint");
        CHECK(cooked->propNames[1] == u8"metallic");
        CHECK(cooked->propNames[2] == u8"albedoMap");
        CHECK(cooked->propTypes[2] == static_cast<u8>(MaterialPropertyType::Texture2D));
        REQUIRE(cooked->uniformDefaults.Size() == 20);                        // float4 + float
        CHECK(*reinterpret_cast<const f32*>(cooked->uniformDefaults.Data() + 16) == doctest::Approx(0.7f));
    }

    RemoveTree();
}
