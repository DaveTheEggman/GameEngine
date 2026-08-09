// Reflection track P1: ScriptClassAsset's reflected surface (its language string).
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
import draconic.core;
import draconic.script.pipeline;

using namespace draconic::core;

TEST_CASE("reflection-p1: ScriptClassAsset exposes language and round-trips")
{
    draconic::pipeline::RegisterScriptAssets();
    const TypeInfo& type = draconic::pipeline::ScriptClassAsset::StaticType();

    CHECK(PropertyCount(type) == 1u);
    const PropertyInfo* lang = FindProperty(type, "language");
    REQUIRE(lang != nullptr);
    CHECK(FindAttribute(*lang, u8"displayName") != nullptr);

    draconic::pipeline::ScriptClassAsset asset;
    Instance inst = Instance::From(&asset);
    CHECK(SetProperty(*lang, inst, Variant::From(String(u8"angelscript"))).IsOk());
    CHECK(GetProperty(*lang, inst).Get<String>().AsView() == StringView(u8"angelscript"));
    CHECK(asset.language.AsView() == StringView(u8"angelscript"));
}
