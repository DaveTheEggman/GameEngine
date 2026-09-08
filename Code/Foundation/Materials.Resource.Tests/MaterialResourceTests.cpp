// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Materials as resources: author a Material, capture it into a MaterialSource that
// references a ShaderSource by id, then build the Material through the ResourceManager
// with both factories registered. Verifies the cooked Material resolves the shader's
// name + properties + default uniforms, and that binding the shader mid-build recorded
// a material->shader dependency edge (so a shader reload propagates). Real DXC + Null RHI.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.rhi;
import foundation.rhi.null;
import foundation.shaders;
import foundation.shaders.system;
import foundation.shaders.resource;
import foundation.materials;
import foundation.materials.resource;

using namespace foundation::core;
using namespace foundation::vfs;
using namespace foundation::resource;
using namespace foundation::materials;
namespace rhi = foundation::rhi;
namespace shaders = foundation::shaders;

namespace
{
    constexpr const char8_t* kVtx =
        u8"float4 main(uint id : SV_VertexID) : SV_Position { return float4(0,0,0,1); }\n";
    constexpr const char8_t* kFrag = u8"float4 main() : SV_Target { return float4(1,0,0,1); }\n";

    void RemoveTree()
    {
        FileDelete(u8"scratch_mat_res_db/lit_shader.rasset");
        FileDelete(u8"scratch_mat_res_db/lit_mat.rasset");
        RemoveDirectory(u8"scratch_mat_res_db");
    }
}

TEST_CASE("material resource: built via the manager; resolves shader + records the dependency")
{
    shaders::Compiler* compiler = nullptr;
    if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk())
    {
        MESSAGE("DXC unavailable; skipping");
        return;
    }

    GlobalTypeRegistry().Register(shaders::ShaderSource::StaticType());
    RegisterSerializable<shaders::ShaderSource>();
    GlobalTypeRegistry().Register(shaders::ShaderResource::StaticType());
    GlobalTypeRegistry().Register(MaterialSource::StaticType());
    RegisterSerializable<MaterialSource>();
    GlobalTypeRegistry().Register(Material::StaticType());

    RemoveTree();
    NativeFileSystem mount(u8"scratch_mat_res_db", DefaultAllocator());

    Guid shaderId, matId;
    {
        foundation::content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");

        auto* shaderInst =
            db.RootGroup()->CreateInstance(u8"lit_shader", shaders::ShaderSource::StaticType());
        shaderId = shaderInst->Id();
        shaders::ShaderSource ss;
        ss.name = String(u8"lit");
        ss.vertexSource = String(kVtx);
        ss.fragmentSource = String(kFrag);
        REQUIRE(shaderInst->WriteObject(ss).IsOk());

        // author a material in code, then capture it into a MaterialSource referencing the shader
        RefPtr<Material> authored = MaterialBuilder(u8"litMat")
                                        .Shader(u8"lit")
                                        .Color(u8"tint", Float4{0.25f, 0.5f, 0.75f, 1.0f})
                                        .Float(u8"roughness", 0.4f)
                                        .Texture(u8"albedoMap")
                                        .Build();

        MaterialSource ms;
        MaterialSource::FromMaterial(*authored, shaderId, ms);

        auto* matInst = db.RootGroup()->CreateInstance(u8"lit_mat", MaterialSource::StaticType());
        matId = matInst->Id();
        REQUIRE(matInst->WriteObject(ms).IsOk());
    }

    foundation::content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(),
                                          u8".rasset");
    rhi::null::NullDevice device{DefaultAllocator()};
    shaders::ShaderSystem system(*compiler, device);
    shaders::ShaderFactory shaderFactory(system);
    MaterialFactory materialFactory;
    ResourceManager manager(DefaultAllocator(), db);
    manager.AddFactory(&shaderFactory);
    manager.AddFactory(&materialFactory);

    Proxy<Material> mat = manager.Bind<Material>(matId);
    REQUIRE(mat);
    CHECK(mat->name == u8"litMat");
    CHECK(mat->shaderName == u8"lit"); // resolved from the bound ShaderResource
    CHECK(mat->PropertyCount() == 3);
    CHECK(mat->UniformDataSize() == 32); // float4 (16) + float (4), 16-byte rounded

    // default uniform data survived the round-trip (tint = 0.25,0.5,0.75,1 at offset 0)
    const Span<const u8> defaults = mat->DefaultUniformData();
    REQUIRE(defaults.Size() == 32);
    const f32* tint = reinterpret_cast<const f32*>(defaults.Data());
    CHECK(tint[0] == doctest::Approx(0.25f));
    CHECK(tint[2] == doctest::Approx(0.75f));
    CHECK(*reinterpret_cast<const f32*>(defaults.Data() + 16) ==
          doctest::Approx(0.4f)); // roughness

    // the factory's Bind of the shader recorded material -> shader
    const Span<const Guid> dependents = manager.Dependents(shaderId);
    bool found = false;
    for (const Guid& g : dependents)
    {
        if (g == matId)
        {
            found = true;
        }
    }
    CHECK(found);

    // reloading the shader is accepted (and transitively touches the material)
    CHECK(manager.Reload(shaderId));
    Proxy<Material> matAfter = manager.Bind<Material>(matId);
    REQUIRE(matAfter);
    CHECK(matAfter->shaderName == u8"lit");

    RemoveTree();
    compiler->Destroy();
}

TEST_CASE("material: CreatePBR packs EmissiveColor at the shader's cbuffer offset")
{
    // The forward cbuffer: BaseColor(0..16), Metallic(16..20), Roughness(20..24), pads to 32,
    // EmissiveColor(32..48). The builder's std140-ish packing must land the same offsets.
    RefPtr<Material> pbr = CreatePBR(u8"m");
    const MaterialPropertyDef* emissive = pbr->FindProperty(u8"EmissiveColor");
    REQUIRE(emissive != nullptr);
    CHECK(emissive->offset == 32u);
    CHECK(emissive->size == 16u);
    // The straggler scalars pack sequentially into the row after EmissiveColor.
    const MaterialPropertyDef* occ = pbr->FindProperty(u8"OcclusionStrength");
    const MaterialPropertyDef* ns = pbr->FindProperty(u8"NormalScale");
    const MaterialPropertyDef* ac = pbr->FindProperty(u8"AlphaCutoff");
    REQUIRE(occ != nullptr);
    REQUIRE(ns != nullptr);
    REQUIRE(ac != nullptr);
    CHECK(occ->offset == 48u);
    CHECK(ns->offset == 52u);
    CHECK(ac->offset == 56u);
    // Black default: adding the field changes nothing visually.
    const Span<const u8> defaults = pbr->DefaultUniformData();
    REQUIRE(defaults.Size() >= 48u);
    f32 rgb[3];
    MemCopy(rgb, defaults.Data() + 32, sizeof(rgb));
    CHECK(rgb[0] == 0.0f);
    CHECK(rgb[1] == 0.0f);
    CHECK(rgb[2] == 0.0f);
}

TEST_CASE("material: a forward source missing the forward properties is STALE (refused, never upgraded)")
{
    // An asset authored BEFORE EmissiveColor existed: the old CreatePBR property set.
    RefPtr<Material> old = MaterialBuilder(u8"legacy")
                               .Shader(u8"forward")
                               .VertexLayout(VertexLayoutType::Mesh)
                               .Color(u8"BaseColor", Float4{0.5f, 0.25f, 0.125f, 1.0f})
                               .Float(u8"Metallic", 1.0f)
                               .Float(u8"Roughness", 0.25f)
                               .Texture(u8"AlbedoMap")
                               .Sampler(u8"MainSampler")
                               .Build();
    MaterialSource src;
    MaterialSource::FromMaterial(*old, Guid{}, src);
    src.shaderName = String(u8"forward");
    StringView missing;
    CHECK_FALSE(ForwardMaterialSourceIsComplete(src, &missing));
    CHECK(missing == StringView(u8"EmissiveColor")); // the refusal names what to fix
    CHECK(src.propertyNames.Size() == 5u);            // nothing appended

    // A current CreatePBR source carries the full table; non-forward shaders are untouched.
    RefPtr<Material> pbr = CreatePBR(u8"current");
    MaterialSource current;
    MaterialSource::FromMaterial(*pbr, Guid{}, current);
    current.shaderName = String(u8"forward");
    CHECK(ForwardMaterialSourceIsComplete(current));
    MaterialSource unlit;
    unlit.shaderName = String(u8"unlit");
    CHECK(ForwardMaterialSourceIsComplete(unlit));
}

TEST_CASE("material source: sampler address modes round-trip")
{
    using namespace foundation::materials;
    NativeFileSystem mount(u8"scratch_mat_sampler_db", DefaultAllocator());

    Guid id;
    {
        foundation::content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        MaterialSource source;
        source.name = u8"wrapped";
        source.shaderName = u8"forward";
        source.samplerU = 2; // rhi::AddressMode::ClampToEdge
        source.samplerV = 1; // rhi::AddressMode::MirrorRepeat
        auto* inst = db.RootGroup()->CreateInstance(u8"wrapped", MaterialSource::StaticType());
        id = inst->Id();
        REQUIRE(inst->WriteObject(source).IsOk());
    }
    {
        foundation::content::ContentDatabase db(foundation::core::DefaultAllocator(), mount, foundation::core::BinarySerializerFactory(),
                                              u8".rasset");
        RefPtr<ISerializable> object = db.ReadObject(id);
        auto* read = Cast<MaterialSource>(object.Get());
        REQUIRE(read != nullptr);
        CHECK(read->samplerU == 2); // the v2 envelope round-trips the modes
        CHECK(read->samplerV == 1);
    }

    FileDelete(u8"scratch_mat_sampler_db/wrapped.rasset");
    RemoveDirectory(u8"scratch_mat_sampler_db");
}

TEST_CASE("material resource: the retyped render-state enum fields round-trip byte-identically")
{
    // u8 -> BlendMode/DepthMode/CullModeConfig/VertexLayoutType (Fable Q2). Each enum serializes as
    // its underlying u8, so the wire is unchanged and old bytes load into the enum. These four are
    // serialized unconditionally (before the version-gated sampler fields), so version is irrelevant.
    MaterialSource out;
    out.blendMode = BlendMode::Additive;               // != default Opaque
    out.depthMode = DepthMode::WriteOnly;              // != default ReadWrite
    out.cullMode = CullModeConfig::Front;              // != default Back
    out.vertexLayout = VertexLayoutType::SkinnedMesh;  // != default Mesh

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        out.Serialize(writer);
    }
    MaterialSource in;
    {
        (void)buffer.Seek(0, SeekOrigin::Begin);
        BinarySerializer reader(buffer, SerializeMode::Read);
        in.Serialize(reader);
    }

    CHECK(in.blendMode == BlendMode::Additive);
    CHECK(in.depthMode == DepthMode::WriteOnly);
    CHECK(in.cullMode == CullModeConfig::Front);
    CHECK(in.vertexLayout == VertexLayoutType::SkinnedMesh);
}
