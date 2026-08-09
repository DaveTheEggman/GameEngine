// Reflection track P1: ShaderAsset's reflected surface (name + fragmentFile strings).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.shaders.pipeline;

using namespace draconic::core;
using namespace draconic::pipeline;

TEST_CASE("reflection-p1: ShaderAsset exposes name + fragmentFile with labels, and round-trips")
{
    draconic::pipeline::RegisterShaderAsset();
    const TypeInfo& type = draconic::pipeline::ShaderAsset::StaticType();

    CHECK(PropertyCount(type) == 2u);
    const PropertyInfo* name = FindProperty(type, "name");
    const PropertyInfo* frag = FindProperty(type, "fragmentFile");
    REQUIRE(name != nullptr);
    REQUIRE(frag != nullptr);
    CHECK(FindAttribute(*frag, u8"displayName") != nullptr);

    draconic::pipeline::ShaderAsset asset;
    Instance inst = Instance::From(&asset);
    CHECK(SetProperty(*name, inst, Variant::From(String(u8"Lit"))).IsOk());
    CHECK(asset.name.AsView() == StringView(u8"Lit"));
    CHECK(SetProperty(*frag, inst, Variant::From(String(u8"lit.frag.hlsl"))).IsOk());
    CHECK(GetProperty(*frag, inst).Get<String>().AsView() == StringView(u8"lit.frag.hlsl"));
}
