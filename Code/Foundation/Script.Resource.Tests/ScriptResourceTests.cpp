// foundation.script.resource tests: the tagged property-value wire (every kind, symmetric
// writer/reader), harvested-metadata round-trip through the cooked record, and the
// ScriptClassFactory (cooked record -> runtime product; metadata parse only, no VM).

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <initializer_list>

import foundation.core;
import foundation.vfs;
import foundation.content;
import foundation.resource;
import foundation.script.resource;

using namespace foundation::core;
using namespace foundation::script;
namespace content = foundation::content;

namespace
{
    [[nodiscard]] ScriptPropertyValue RoundTrip(const ScriptPropertyValue& in)
    {
        MemoryStream stream;
        {
            BinarySerializer w(stream, SerializeMode::Write);
            ScriptPropertyValue copy = in;
            Serialize(w, copy);
            REQUIRE(w.IsOk());
        }
        (void)stream.Seek(0, SeekOrigin::Begin);
        ScriptPropertyValue out;
        {
            BinarySerializer r(stream, SerializeMode::Read);
            Serialize(r, out);
            REQUIRE(r.IsOk());
        }
        return out;
    }

    void RemoveDbTree(StringView dir)
    {
        for (const utf8char* name : {u8"cooked.rasset"})
        {
            String path(dir);
            path.Append(u8"/");
            path.Append(name);
            FileDelete(path.AsView());
        }
        RemoveDirectory(dir);
    }
}

TEST_CASE("script.resource: property values round-trip for every kind (symmetric wire)")
{
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Float;
        v.number = 4.25;
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Int;
        v.number = -12.0;
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Bool;
        v.boolean = true;
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::String;
        v.text = String(u8"héllo wörld");
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Color;
        v.color = Color{0.25f, 0.5f, 0.75f, 0.5f};
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Vec3;
        v.vector = Float3{1.0f, -2.0f, 3.5f};
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Entity;
        v.guid = Guid{0x12, 0x34};
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v;
        v.kind = ScriptPropertyType::Asset;
        v.guid = Guid{0xAB, 0xCD};
        CHECK(ScriptPropertyValuesEqual(RoundTrip(v), v));
    }
    {
        ScriptPropertyValue v; // None writes no payload and reads back as None
        CHECK(RoundTrip(v).kind == ScriptPropertyType::None);
    }
}

TEST_CASE("script.resource: the type-string parser covers the v1 set (+ asset:<T>)")
{
    ScriptPropertyType kind{};
    String assetType;
    CHECK(ParseScriptPropertyType(u8"float", kind, assetType));
    CHECK(kind == ScriptPropertyType::Float);
    CHECK(ParseScriptPropertyType(u8"int", kind, assetType));
    CHECK(kind == ScriptPropertyType::Int);
    CHECK(ParseScriptPropertyType(u8"bool", kind, assetType));
    CHECK(ParseScriptPropertyType(u8"string", kind, assetType));
    CHECK(ParseScriptPropertyType(u8"color", kind, assetType));
    CHECK(ParseScriptPropertyType(u8"vec3", kind, assetType));
    CHECK(ParseScriptPropertyType(u8"entity", kind, assetType));
    CHECK(kind == ScriptPropertyType::Entity);
    REQUIRE(ParseScriptPropertyType(u8"asset:AudioClip", kind, assetType));
    CHECK(kind == ScriptPropertyType::Asset);
    CHECK(assetType == u8"AudioClip");
    CHECK_FALSE(ParseScriptPropertyType(u8"quaternion", kind, assetType));
    CHECK_FALSE(ParseScriptPropertyType(u8"asset:", kind, assetType));
}

namespace
{
    // The ScriptClassFactory is backend-NEUTRAL: it reconstructs a runtime ScriptClass from the
    // cooked ScriptClassSource METADATA and never compiles the source (no backend is linked here),
    // so the same record->factory->product round-trip holds for a record of EITHER language. Driven
    // once per backend label to prove the resource layer carries no backend assumption.
    void DriveFactoryRoundTrip(StringView language, StringView sourceName, StringView source)
    {
        RegisterScriptResource();
        String outDir(u8"scratch_scriptres_out_");
        outDir += language;
        RemoveDbTree(outDir.AsView());
        foundation::vfs::NativeFileSystem outputMount(outDir.AsView());
        content::ContentDatabase outputDb(outputMount, BinarySerializerFactory(), u8".rasset");

        ScriptClassSource cooked;
        cooked.language = String(language);
        cooked.className = String(u8"Mover");
        cooked.sourceName = String(sourceName); // the source-file identity (breakpoint key)
        cooked.source = String(source);         // stored verbatim, never compiled here
        {
            ScriptPropertyDesc speed;
            speed.name = String(u8"speed");
            speed.hash = ScriptPropertyNameHash(u8"speed");
            speed.type = ScriptPropertyType::Float;
            speed.defaultValue.kind = ScriptPropertyType::Float;
            speed.defaultValue.number = 4.0;
            speed.description = String(u8"units per second");
            cooked.properties.PushBack(Move(speed));

            ScriptPropertyDesc clip;
            clip.name = String(u8"clip");
            clip.hash = ScriptPropertyNameHash(u8"clip");
            clip.type = ScriptPropertyType::Asset;
            clip.assetType = String(u8"AudioClip");
            clip.defaultValue.kind = ScriptPropertyType::Asset;
            cooked.properties.PushBack(Move(clip));
        }
        cooked.handlers.PushBack(String(u8"onStart"));
        cooked.handlers.PushBack(String(u8"onUpdate"));
        cooked.usesCoroutines = true;

        auto* instance =
            outputDb.RootGroup()->CreateInstance(u8"cooked", ScriptClassSource::StaticType());
        REQUIRE(instance != nullptr);
        REQUIRE(instance->WriteObject(cooked).IsOk());

        ScriptClassFactory factory;
        foundation::resource::ResourceManager manager(outputDb);
        manager.AddFactory(&factory);
        foundation::resource::Proxy<ScriptClass> product =
            manager.Bind<ScriptClass>(instance->Id());
        REQUIRE(product);
        CHECK(product->language == language);
        CHECK(product->className == u8"Mover");
        CHECK(product->sourceName == sourceName); // travels the cooked wire (symmetric)
        CHECK(product->source == source);
        REQUIRE(product->properties.Size() == 2u);
        const ScriptPropertyDesc* speed = product->FindProperty(ScriptPropertyNameHash(u8"speed"));
        REQUIRE(speed != nullptr);
        CHECK(speed->type == ScriptPropertyType::Float);
        CHECK(speed->defaultValue.number == doctest::Approx(4.0));
        CHECK(speed->description == u8"units per second");
        const ScriptPropertyDesc* clip = product->FindProperty(ScriptPropertyNameHash(u8"clip"));
        REQUIRE(clip != nullptr);
        CHECK(clip->type == ScriptPropertyType::Asset);
        CHECK(clip->assetType == u8"AudioClip");
        CHECK(product->HasHandler(u8"onStart"));
        CHECK(product->HasHandler(u8"onUpdate"));
        CHECK_FALSE(product->HasHandler(u8"onDestroy"));
        CHECK(product->usesCoroutines); // travels the cooked wire (symmetric)
        CHECK(product->ProfileName() != nullptr);

        RemoveDbTree(outDir.AsView());
    }
}

TEST_CASE("script.resource: cooked record -> factory -> runtime product, angelscript label "
          "(metadata parse, hash lookups, handler set)")
{
    DriveFactoryRoundTrip(u8"angelscript", u8"Mover.as", u8"class Mover {\n Mover(Entity@ e) {}\n}\n");
}

TEST_CASE("script.resource: cooked record -> factory -> runtime product, luau label "
          "(the resource layer carries no backend assumption)")
{
    DriveFactoryRoundTrip(
        u8"luau", u8"Mover.luau",
        u8"Mover = {}\nMover.__index = Mover\n"
        u8"function Mover.new(entity) return setmetatable({}, Mover) end\n");
}
