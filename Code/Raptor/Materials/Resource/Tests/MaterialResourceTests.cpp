// Materials as resources: author a Material, capture it into a MaterialSource that
// references a ShaderSource by id, then build the Material through the ResourceManager
// with both factories registered. Verifies the cooked Material resolves the shader's
// name + properties + default uniforms, and that binding the shader mid-build recorded
// a material->shader dependency edge (so a shader reload propagates). Real DXC + Null RHI.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.vfs;
import raptor.content;
import raptor.resource;
import raptor.rhi;
import raptor.rhi.null;
import raptor.shaders;
import raptor.shaders.system;
import raptor.shaders.resource;
import raptor.materials;
import raptor.materials.resource;

using namespace raptor::core;
using namespace raptor::vfs;
using namespace raptor::resource;
using namespace raptor::materials;
namespace rhi = raptor::rhi;
namespace shaders = raptor::shaders;

namespace
{
    constexpr const char8_t* kVtx  = u8"float4 main(uint id : SV_VertexID) : SV_Position { return float4(0,0,0,1); }\n";
    constexpr const char8_t* kFrag = u8"float4 main() : SV_Target { return float4(1,0,0,1); }\n";

    void RemoveTree()
    {
        FileDelete(u8"raptor_mat_res_db/lit_shader.rasset");
        FileDelete(u8"raptor_mat_res_db/lit_mat.rasset");
        RemoveDirectory(u8"raptor_mat_res_db");
    }
}

TEST_CASE("material resource: built via the manager; resolves shader + records the dependency")
{
    shaders::Compiler* compiler = nullptr;
    if (!shaders::createCompiler(shaders::CompilerDesc{}, compiler).IsOk()) { MESSAGE("DXC unavailable; skipping"); return; }

    GlobalTypeRegistry().Register(shaders::ShaderSource::StaticType());
    RegisterSerializable<shaders::ShaderSource>();
    GlobalTypeRegistry().Register(shaders::ShaderResource::StaticType());
    GlobalTypeRegistry().Register(MaterialSource::StaticType());
    RegisterSerializable<MaterialSource>();
    GlobalTypeRegistry().Register(Material::StaticType());

    RemoveTree();
    NativeFileSystem mount(u8"raptor_mat_res_db");

    Guid shaderId, matId;
    {
        raptor::content::ContentDatabase db(mount);

        auto* shaderInst = db.RootGroup()->CreateInstance(u8"lit_shader", shaders::ShaderSource::StaticType());
        shaderId = shaderInst->Id();
        shaders::ShaderSource ss;
        ss.name = String(u8"lit");
        ss.vertexSource = String(kVtx);
        ss.fragmentSource = String(kFrag);
        REQUIRE(shaderInst->WriteObject(ss).IsOk());

        // author a material in code, then capture it into a MaterialSource referencing the shader
        RefPtr<Material> authored = MaterialBuilder(u8"litMat")
            .Shader(u8"lit")
            .Color(u8"tint", Vec4{ 0.25f, 0.5f, 0.75f, 1.0f })
            .Float(u8"roughness", 0.4f)
            .Texture(u8"albedoMap")
            .Build();

        MaterialSource ms;
        MaterialSource::FromMaterial(*authored, shaderId, ms);

        auto* matInst = db.RootGroup()->CreateInstance(u8"lit_mat", MaterialSource::StaticType());
        matId = matInst->Id();
        REQUIRE(matInst->WriteObject(ms).IsOk());
    }

    raptor::content::ContentDatabase db(mount);
    rhi::null::NullDevice device;
    shaders::ShaderSystem system(*compiler, device);
    shaders::ShaderFactory shaderFactory(system);
    MaterialFactory materialFactory;
    ResourceManager manager(db);
    manager.AddFactory(&shaderFactory);
    manager.AddFactory(&materialFactory);

    Proxy<Material> mat = manager.Bind<Material>(matId);
    REQUIRE(mat);
    CHECK(mat->name == u8"litMat");
    CHECK(mat->shaderName == u8"lit");                  // resolved from the bound ShaderResource
    CHECK(mat->PropertyCount() == 3);
    CHECK(mat->UniformDataSize() == 20);                // float4 (16) + float (4)

    // default uniform data survived the round-trip (tint = 0.25,0.5,0.75,1 at offset 0)
    const Span<const u8> defaults = mat->DefaultUniformData();
    REQUIRE(defaults.Size() == 20);
    const f32* tint = reinterpret_cast<const f32*>(defaults.Data());
    CHECK(tint[0] == doctest::Approx(0.25f));
    CHECK(tint[2] == doctest::Approx(0.75f));
    CHECK(*reinterpret_cast<const f32*>(defaults.Data() + 16) == doctest::Approx(0.4f));   // roughness

    // the factory's Bind of the shader recorded material -> shader
    const Span<const Guid> dependents = manager.Dependents(shaderId);
    bool found = false;
    for (const Guid& g : dependents) { if (g == matId) { found = true; } }
    CHECK(found);

    // reloading the shader is accepted (and transitively touches the material)
    CHECK(manager.Reload(shaderId));
    Proxy<Material> matAfter = manager.Bind<Material>(matId);
    REQUIRE(matAfter);
    CHECK(matAfter->shaderName == u8"lit");

    RemoveTree();
    compiler->Destroy();
}
