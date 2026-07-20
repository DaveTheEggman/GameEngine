// draconic.script.editor tests: the cook helpers (class-name/handler scan, harvest
// record parse), the FULL harvest round-trip (source -> builder's cooker VM -> cooked
// record -> factory -> runtime metadata), the compile-error path (cook FAILS, the last
// good cooked record survives), and B3 backend-neutrality (the builder resolves its
// harvest VM through the ScriptBackendRegistry by the asset's LANGUAGE).

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <initializer_list>

import draconic.core;
import draconic.vfs;
import draconic.content;
import draconic.resource;
import draconic.editor;
import draconic.editor.core;
import draconic.script;
import draconic.script.wren;
import draconic.script.resource;
import draconic.script.editor;

using namespace draconic::core;
using namespace draconic::script;
namespace content = draconic::content;

namespace
{
    constexpr StringView kMoverSource =
        u8"// A behavior with the full property spread.\n"
        u8"class Mover {\n"
        u8"    static properties { {\n"
        u8"        \"speed\":  [\"float\", 4.5, \"units per second\"],\n"
        u8"        \"count\":  [\"int\", 3],\n"
        u8"        \"active\": [\"bool\", true],\n"
        u8"        \"label\":  [\"string\", \"hi\"],\n"
        u8"        \"tint\":   [\"color\", [0.25, 0.5, 0.75, 1]],\n"
        u8"        \"offset\": [\"vec3\", [1, 2, 3]],\n"
        u8"        \"target\": [\"entity\", null],\n"
        u8"        \"clip\":   [\"asset:AudioClip\", null],\n"
        u8"    } }\n"
        u8"\n"
        u8"    construct new(entity) { _entity = entity }\n"
        u8"    speed=(v) { _speed = v }\n"
        u8"    onStart() {}\n"
        u8"    onUpdate(dt) {}\n"
        u8"    onDestroy() {}\n"
        u8"}\n";

    void RemoveTree(StringView dir)
    {
        for (const utf8char* name : { u8"mover.wren", u8"cooked.rasset", u8"util.wren",
                                      u8"broken.wren", u8"fake.ftl", u8"starter.wren" })
        {
            String path(dir);
            path.Append(u8"/");
            path.Append(name);
            FileDelete(path.AsView());
        }
        RemoveDirectory(dir);
    }

    struct CookBed
    {
        String srcDir;
        String outDir;
        UniquePtr<draconic::vfs::NativeFileSystem> sources;
        UniquePtr<draconic::vfs::NativeFileSystem> output;
        UniquePtr<content::ContentDatabase> outputDb;

        explicit CookBed(StringView tag)
        {
            // Cook-error logs reach the test output (silent otherwise).
            static bool logReady = []() {
                static ConsoleSink sink;
                GlobalLogger().AddSink(&sink);
                return true;
            }();
            (void)logReady;
            draconic::script::wren::RegisterWrenScriptBackend();
            RegisterScriptResource();
            RegisterScriptAssets();
            srcDir = String(u8"draconic_scriptpipe_src_");
            srcDir += tag;
            outDir = String(u8"draconic_scriptpipe_out_");
            outDir += tag;
            RemoveTree(srcDir.AsView());
            RemoveTree(outDir.AsView());
            REQUIRE(CreateDirectory(srcDir.AsView()));
            sources = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(), srcDir.AsView());
            output = MakeUnique<draconic::vfs::NativeFileSystem>(DefaultAllocator(), outDir.AsView());
            outputDb = MakeUnique<content::ContentDatabase>(DefaultAllocator(), *output,
                                                            BinarySerializerFactory(), u8".rasset");
        }
        ~CookBed()
        {
            outputDb = nullptr;
            RemoveTree(srcDir.AsView());
            RemoveTree(outDir.AsView());
        }

        void WriteSource(StringView fileName, StringView text)
        {
            String path(srcDir.AsView());
            path.Append(u8"/");
            path.Append(fileName);
            REQUIRE(WriteFile(path.AsView(),
                              Span<const byte>(reinterpret_cast<const byte*>(text.Data()),
                                               text.Size())).IsOk());
        }

        [[nodiscard]] Status Cook(StringView fileName, StringView language,
                                  content::Instance*& outInstance)
        {
            ScriptClassAsset asset;
            asset.fileName = String(fileName);
            asset.language = String(language);
            ScriptClassAssetBuilder builder;
            draconic::editor::AssetBuildContext ctx;
            ctx.sources = sources.Get();
            if (outInstance == nullptr)
            {
                outInstance = outputDb->RootGroup()->CreateInstance(
                    u8"cooked", ScriptClassSource::StaticType());
            }
            ctx.output = outInstance;
            return builder.Build(asset, ctx);
        }
    };
}

TEST_CASE("script.pipeline: class-name scan prefers the file stem, falls back to the "
          "first class, ignores comments")
{
    CHECK(FindScriptClassName(u8"class Mover {\n}\n", u8"Mover") == u8"Mover");
    CHECK(FindScriptClassName(u8"class Helper {\n}\nclass Mover {\n}\n", u8"Mover") == u8"Mover");
    CHECK(FindScriptClassName(u8"class Helper {\n}\nclass Other {\n}\n", u8"Mover") == u8"Helper");
    CHECK(FindScriptClassName(u8"// class Fake {\nclass Real {\n}\n", u8"nope") == u8"Real");
    CHECK(FindScriptClassName(u8"/* class Fake { */\nclass Real {\n}\n", u8"nope") == u8"Real");
    CHECK(FindScriptClassName(u8"var x = 1\n", u8"Mover").IsEmpty());
}

TEST_CASE("script.pipeline: handler scan finds declared handlers only (comments stripped)")
{
    const Array<String> handlers = ScanScriptHandlers(
        u8"class A {\n"
        u8"    onStart() {}\n"
        u8"    onUpdate(dt) {}\n"
        u8"    // onDestroy() would be nice\n"
        u8"    /* onEnable() {} */\n"
        u8"}\n");
    auto has = [&handlers](StringView name) {
        for (const String& h : handlers) { if (h.AsView() == name) { return true; } }
        return false;
    };
    CHECK(has(u8"onStart"));
    CHECK(has(u8"onUpdate"));
    CHECK_FALSE(has(u8"onDestroy"));
    CHECK_FALSE(has(u8"onEnable"));
    CHECK_FALSE(has(u8"onDisable"));
}

TEST_CASE("script.pipeline: handler scan captures the whole on<Upper>(...) convention - "
          "custom message + event handlers, not lookalikes (P2)")
{
    const Array<String> handlers = ScanScriptHandlers(
        u8"class A {\n"
        u8"    onStart() {}\n"
        u8"    onHeal(amount) {}\n"            // custom message handler (entity.send)
        u8"    onContactBegin(o, p, n) {}\n"   // physics event handler
        u8"    onlyOnce() {}\n"                // lowercase after 'on' - NOT a handler
        u8"    onFoo {}\n"                     // getter (no parens) - NOT a handler
        u8"    speed=(v) {}\n"                 // setter - NOT a handler
        u8"}\n");
    auto has = [&handlers](StringView name) {
        for (const String& h : handlers) { if (h.AsView() == name) { return true; } }
        return false;
    };
    CHECK(has(u8"onStart"));
    CHECK(has(u8"onHeal"));
    CHECK(has(u8"onContactBegin"));
    CHECK_FALSE(has(u8"onlyOnce"));
    CHECK_FALSE(has(u8"onFoo"));
    CHECK(handlers.Size() == 3u);
}

TEST_CASE("script.pipeline: coroutine use is harvested (is Behavior / startCoroutine, "
          "comments ignored)")
{
    // Wren: extending the base OR referencing startCoroutine( flags it.
    CHECK(ScriptUsesCoroutines(u8"class Mover is Behavior {\n}\n", u8"wren"));
    CHECK(ScriptUsesCoroutines(
        u8"class Mover {\n    onStart() { startCoroutine(Fn.new {}) }\n}\n", u8"wren"));
    // A plain P1 behavior does not.
    CHECK_FALSE(ScriptUsesCoroutines(
        u8"class Mover {\n    onUpdate(dt) {}\n}\n", u8"wren"));
    // `is Behavior` only counts in real code, not a comment.
    CHECK_FALSE(ScriptUsesCoroutines(
        u8"// class Mover is Behavior\nclass Mover {\n}\n", u8"wren"));
    // A non-Wren language ignores the Wren-only `is Behavior` token but still catches
    // the shared startCoroutine( surface.
    CHECK_FALSE(ScriptUsesCoroutines(u8"class Mover is Behavior {}\n", u8"angelscript"));
    CHECK(ScriptUsesCoroutines(u8"void begin() { startCoroutine(@this.Run); }\n",
                               u8"angelscript"));
}

TEST_CASE("script.pipeline: full harvest round-trip - source -> cook -> factory -> "
          "typed metadata (sorted, hashed, described)")
{
    CookBed bed(u8"harvest");
    bed.WriteSource(u8"mover.wren", kMoverSource);
    content::Instance* instance = nullptr;
    REQUIRE(bed.Cook(u8"mover.wren", u8"wren", instance).IsOk());

    ScriptClassFactory factory;
    draconic::resource::ResourceManager manager(*bed.outputDb);
    manager.AddFactory(&factory);
    draconic::resource::Proxy<ScriptClass> product = manager.Bind<ScriptClass>(instance->Id());
    REQUIRE(product);
    CHECK(product->language == u8"wren");
    CHECK(product->className == u8"Mover");
    CHECK(product->source == kMoverSource);
    REQUIRE(product->properties.Size() == 8u);

    // Sorted by name (deterministic cooked bytes regardless of Wren map order).
    for (usize i = 1; i < product->properties.Size(); ++i)
    {
        const StringView a = product->properties[i - 1].name.AsView();
        const StringView b = product->properties[i].name.AsView();
        bool ordered = false;
        const usize n = a.Size() < b.Size() ? a.Size() : b.Size();
        for (usize k = 0; k <= n; ++k)
        {
            if (k == n) { ordered = a.Size() <= b.Size(); break; }
            if (a[k] != b[k]) { ordered = a[k] < b[k]; break; }
        }
        CHECK(ordered);
    }

    const ScriptPropertyDesc* speed = product->FindProperty(ScriptPropertyNameHash(u8"speed"));
    REQUIRE(speed != nullptr);
    CHECK(speed->type == ScriptPropertyType::Float);
    CHECK(speed->defaultValue.number == doctest::Approx(4.5));
    CHECK(speed->description == u8"units per second");

    const ScriptPropertyDesc* count = product->FindProperty(ScriptPropertyNameHash(u8"count"));
    REQUIRE(count != nullptr);
    CHECK(count->type == ScriptPropertyType::Int);
    CHECK(count->defaultValue.number == doctest::Approx(3.0));

    const ScriptPropertyDesc* active = product->FindProperty(ScriptPropertyNameHash(u8"active"));
    REQUIRE(active != nullptr);
    CHECK(active->type == ScriptPropertyType::Bool);
    CHECK(active->defaultValue.boolean);

    const ScriptPropertyDesc* label = product->FindProperty(ScriptPropertyNameHash(u8"label"));
    REQUIRE(label != nullptr);
    CHECK(label->type == ScriptPropertyType::String);
    CHECK(label->defaultValue.text == u8"hi");

    const ScriptPropertyDesc* tint = product->FindProperty(ScriptPropertyNameHash(u8"tint"));
    REQUIRE(tint != nullptr);
    CHECK(tint->type == ScriptPropertyType::Color);
    CHECK(tint->defaultValue.color.g == doctest::Approx(0.5f));

    const ScriptPropertyDesc* offset = product->FindProperty(ScriptPropertyNameHash(u8"offset"));
    REQUIRE(offset != nullptr);
    CHECK(offset->type == ScriptPropertyType::Vec3);
    CHECK(offset->defaultValue.vector.z == doctest::Approx(3.0f));

    const ScriptPropertyDesc* target = product->FindProperty(ScriptPropertyNameHash(u8"target"));
    REQUIRE(target != nullptr);
    CHECK(target->type == ScriptPropertyType::Entity);
    CHECK(target->defaultValue.guid.IsNil());

    const ScriptPropertyDesc* clip = product->FindProperty(ScriptPropertyNameHash(u8"clip"));
    REQUIRE(clip != nullptr);
    CHECK(clip->type == ScriptPropertyType::Asset);
    CHECK(clip->assetType == u8"AudioClip");

    CHECK(product->HasHandler(u8"onStart"));
    CHECK(product->HasHandler(u8"onUpdate"));
    CHECK(product->HasHandler(u8"onDestroy"));
    CHECK_FALSE(product->HasHandler(u8"onEnable"));
}

TEST_CASE("script.pipeline: facade-using behaviors compile at cook (the cook VM "
          "mirrors the runtime's \"main\" surface, prelude included)")
{
    CookBed bed(u8"facades");
    bed.WriteSource(u8"mover.wren",
                    u8"import \"main\" for Float3\n"
                    u8"class Mover {\n"
                    u8"    construct new(entity) { _entity = entity }\n"
                    u8"    onUpdate(dt) {\n"
                    u8"        Log.info(\"at %(Time.now())\")\n"
                    u8"        var v = Float3.new(Random.value(), 0, 0)\n"
                    u8"        _entity.setPosition(v.x, v.y, v.z)\n"
                    u8"    }\n"
                    u8"}\n");
    content::Instance* instance = nullptr;
    CHECK(bed.Cook(u8"mover.wren", u8"wren", instance).IsOk());
}

TEST_CASE("script.pipeline: a ScriptClass-less utility module cooks with empty metadata")
{
    CookBed bed(u8"util");
    bed.WriteSource(u8"util.wren", u8"var Helper = 42\n");
    content::Instance* instance = nullptr;
    REQUIRE(bed.Cook(u8"util.wren", u8"wren", instance).IsOk());
    RefPtr<ISerializable> object = instance->ReadObject();
    ScriptClassSource* cooked = Cast<ScriptClassSource>(object.Get());
    REQUIRE(cooked != nullptr);
    CHECK(cooked->className.IsEmpty());
    CHECK(cooked->properties.IsEmpty());
    CHECK(cooked->handlers.IsEmpty());
}

TEST_CASE("script.pipeline: compile errors FAIL the cook and the last good record "
          "survives (old class keeps running)")
{
    CookBed bed(u8"errors");
    bed.WriteSource(u8"mover.wren", kMoverSource);
    content::Instance* instance = nullptr;
    REQUIRE(bed.Cook(u8"mover.wren", u8"wren", instance).IsOk());

    // Break the source; the rebuild must FAIL without touching the cooked record.
    bed.WriteSource(u8"mover.wren", u8"class Mover {\n  this is not wren at all(\n");
    CHECK_FALSE(bed.Cook(u8"mover.wren", u8"wren", instance).IsOk());

    RefPtr<ISerializable> object = instance->ReadObject();
    ScriptClassSource* cooked = Cast<ScriptClassSource>(object.Get());
    REQUIRE(cooked != nullptr);                       // the LAST GOOD record
    CHECK(cooked->className == u8"Mover");
    CHECK(cooked->source == kMoverSource);
    CHECK(cooked->properties.Size() == 8u);

    // A faulting `static properties` getter is a cook error too.
    bed.WriteSource(u8"mover.wren",
                    u8"class Mover {\n"
                    u8"    static properties { Fiber.abort(\"boom\") }\n"
                    u8"    construct new(e) {}\n"
                    u8"}\n");
    CHECK_FALSE(bed.Cook(u8"mover.wren", u8"wren", instance).IsOk());

    // An unknown property type is a cook error (the v1 type set is CLOSED).
    bed.WriteSource(u8"mover.wren",
                    u8"class Mover {\n"
                    u8"    static properties { { \"x\": [\"quaternion\", null] } }\n"
                    u8"    construct new(e) {}\n"
                    u8"}\n");
    CHECK_FALSE(bed.Cook(u8"mover.wren", u8"wren", instance).IsOk());
}

TEST_CASE("script.pipeline: B3 - the builder resolves its harvest VM through the "
          "backend REGISTRY by the asset's language")
{
    // A fake language backend: counts creations, compiles anything.
    struct FakeContext final : IScriptContext
    {
        void SetErrorHandler(IScriptErrorHandler*) override {}
        Status Load(StringView, StringView) override { return Status{}; }
        void SetGlobal(StringView, const Variant&) override {}
        Variant GetGlobal(StringView) override { return Variant{}; }
        bool HasFunction(StringView) const override { return false; }
        Result<Variant> Call(StringView, Span<Variant>) override
        {
            return Err(ErrorCode::NotSupported);
        }
        RefPtr<ScriptObject> CreateInstance(StringView, Span<Variant>) override
        {
            return nullptr;
        }
    };
    struct FakeManager final : IScriptManager
    {
        void RegisterType(const TypeInfo&) override {}
        RefPtr<IScriptContext> CreateContext() override
        {
            return RefPtr<IScriptContext>(MakeRef<FakeContext>(DefaultAllocator()));
        }
    };

    static int created = 0;
    created = 0;
    ScriptBackendDesc fake;
    fake.languageId = String(u8"faketest");
    fake.displayName = String(u8"FakeTest");
    fake.fileExtensions.PushBack(String(u8"ftl"));
    fake.create = []() -> RefPtr<IScriptManager> {
        ++created;
        return RefPtr<IScriptManager>(MakeRef<FakeManager>(DefaultAllocator()));
    };
    ScriptBackendRegistry::Get().Register(Move(fake));

    CookBed bed(u8"b3");
    bed.WriteSource(u8"fake.ftl", u8"anything goes - the fake backend accepts it\n");
    content::Instance* instance = nullptr;
    REQUIRE(bed.Cook(u8"fake.ftl", u8"faketest", instance).IsOk());
    CHECK(created == 1);   // the harvest VM came from the REGISTRY, by language

    RefPtr<ISerializable> object = instance->ReadObject();
    ScriptClassSource* cooked = Cast<ScriptClassSource>(object.Get());
    REQUIRE(cooked != nullptr);
    CHECK(cooked->language == u8"faketest");
    CHECK(cooked->properties.IsEmpty());   // no Wren probe for other languages (v1)

    // An asset naming an UNREGISTERED language fails the cook cleanly.
    content::Instance* second = bed.outputDb->RootGroup()->CreateInstance(
        u8"cooked2", ScriptClassSource::StaticType());
    ScriptClassAsset asset;
    asset.fileName = String(u8"fake.ftl");
    asset.language = String(u8"nosuchlang");
    ScriptClassAssetBuilder builder;
    draconic::editor::AssetBuildContext ctx;
    ctx.sources = bed.sources.Get();
    ctx.output = second;
    CHECK_FALSE(builder.Build(asset, ctx).IsOk());

    // The importer accepts exactly the REGISTERED extensions (language-clean).
    ScriptFileImporter importer;
    CHECK(importer.Accepts(u8"wren"));
    CHECK(importer.Accepts(u8"ftl"));
    CHECK_FALSE(importer.Accepts(u8"lua"));
}

TEST_CASE("script.pipeline: the New Asset starter template cooks with its declared "
          "property + handlers")
{
    CookBed bed(u8"starter");
    bed.WriteSource(u8"starter.wren", kScriptBehaviorStarter);
    content::Instance* instance = nullptr;
    REQUIRE(bed.Cook(u8"starter.wren", u8"wren", instance).IsOk());
    RefPtr<ISerializable> object = instance->ReadObject();
    ScriptClassSource* cooked = Cast<ScriptClassSource>(object.Get());
    REQUIRE(cooked != nullptr);
    CHECK(cooked->className == u8"NewBehavior");
    REQUIRE(cooked->properties.Size() == 1u);
    CHECK(cooked->properties[0].name == u8"speed");
    CHECK(cooked->properties[0].type == ScriptPropertyType::Float);
    CHECK(cooked->handlers.Size() == 3u);   // onStart, onUpdate, onDestroy
}
