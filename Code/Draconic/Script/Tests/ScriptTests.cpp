#include <doctest/doctest.h>

#include "Core/Prelude.h"  // <new> reachability for container instantiation (GCC)
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.script;

using namespace draconic::core;
using namespace draconic::script;

// A tiny reflected object to exercise object-valued globals.
namespace
{
    class Widget : public Object
    {
        DRACONIC_OBJECT(Widget, Object)
    public:
        int id = 0;
    };

    // A minimal in-memory backend: no scripting language, just enough to
    // validate the abstraction's data contract (Variant in/out, type registry).
    class MockContext final : public IScriptContext
    {
    public:
        void SetErrorHandler(IScriptErrorHandler*) override {}
        Status Load(StringView, StringView) override { return Status{ ErrorCode::NotSupported }; }
        void SetGlobal(StringView name, const Variant& value) override { m_globals.InsertOrAssign(String(name), value); }
        Variant GetGlobal(StringView name) override
        {
            const Variant* found = m_globals.Find(String(name));
            return (found != nullptr) ? *found : Variant{};
        }
        bool HasFunction(StringView) const override { return false; }
        Result<Variant> Call(StringView, Span<Variant>) override { return Err(ErrorCode::NotSupported); }
        RefPtr<ScriptObject> CreateInstance(StringView, Span<Variant>) override { return nullptr; }

    private:
        HashMap<String, Variant> m_globals;
    };

    class MockManager final : public IScriptManager
    {
    public:
        void RegisterType(const TypeInfo& type) override { m_registered.PushBack(&type); }
        RefPtr<IScriptContext> CreateContext() override
        {
            return RefPtr<IScriptContext>(MakeRef<MockContext>(DefaultAllocator()));
        }

        [[nodiscard]] bool Has(const TypeInfo& type) const
        {
            for (const TypeInfo* t : m_registered) { if (t == &type) { return true; } }
            return false;
        }
        [[nodiscard]] usize Count() const { return m_registered.Size(); }

    private:
        Array<const TypeInfo*> m_registered;
    };
}

DRACONIC_DEFINE_OBJECT(Widget, "draconic::script::test")

TEST_CASE("script: reflected types register with a manager")
{
    RegisterCoreTypes();

    RefPtr<MockManager> manager = MakeRef<MockManager>(DefaultAllocator());
    RegisterReflectedTypes(*manager);

    CHECK(manager->Count() >= 10u);
    CHECK(manager->Has(TypeOf<Float3>()));
    CHECK(manager->Has(TypeOf<Guid>()));
    CHECK(manager->Has(TypeOf<Float4x4>()));
}

TEST_CASE("script: context round-trips value and object globals as Variant")
{
    RefPtr<MockManager> manager = MakeRef<MockManager>(DefaultAllocator());
    RefPtr<IScriptContext> ctx = manager->CreateContext();
    REQUIRE(static_cast<bool>(ctx));

    // Value global.
    ctx->SetGlobal(u8"pos", Variant::From(Float3{ 1.0f, 2.0f, 3.0f }));
    CHECK(ctx->GetGlobal(u8"pos").Get<Float3>() == Float3{ 1.0f, 2.0f, 3.0f });

    // Object global keeps the object alive and reports its dynamic type.
    RefPtr<Widget> widget = MakeRef<Widget>(DefaultAllocator());
    widget->id = 42;
    ctx->SetGlobal(u8"w", Variant::From(widget));
    CHECK(widget->RefCount() == 2u);  // widget + the global's Variant

    Variant got = ctx->GetGlobal(u8"w");
    CHECK(got.IsObject());
    CHECK(got.Type() == &Widget::StaticType());
    REQUIRE(got.AsObject<Widget>() != nullptr);
    CHECK(got.AsObject<Widget>()->id == 42);

    // Missing global -> empty Variant.
    CHECK(ctx->GetGlobal(u8"missing").IsEmpty());
}

TEST_CASE("script: backend reports unsupported operations cleanly")
{
    RefPtr<MockManager> manager = MakeRef<MockManager>(DefaultAllocator());
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    CHECK(ctx->Load(u8"print('hi')", u8"chunk").Code() == ErrorCode::NotSupported);
    CHECK_FALSE(ctx->HasFunction(u8"main"));
    CHECK(ctx->Call(u8"main", Span<Variant>{}).Error() == ErrorCode::NotSupported);
}

namespace
{
    // A registry-conformance fake: records the two-phase order the contract promises.
    class FakeScriptManager final : public draconic::script::IScriptManager
    {
    public:
        u32 registered = 0;
        bool finalized = false;
        bool finalizedAfterAll = false;
        void RegisterType(const TypeInfo&) override
        {
            REQUIRE_FALSE(finalized);   // collection strictly precedes finalize
            ++registered;
        }
        void FinalizeTypes() override
        {
            finalized = true;
            finalizedAfterAll = registered > 0;
        }
        [[nodiscard]] RefPtr<draconic::script::IScriptContext> CreateContext() override
        {
            return {};
        }
    };
}

TEST_CASE("script.backend: registry resolves by language and extension; file dispatch "
          "falls back to a sole backend")
{
    using namespace draconic::script;
    ScriptBackendRegistry& registry = ScriptBackendRegistry::Get();

    ScriptBackendDesc wrenLike;
    wrenLike.languageId = String(u8"testlang");
    wrenLike.displayName = String(u8"TestLang");
    wrenLike.fileExtensions.PushBack(String(u8"tl"));
    int created = 0;
    wrenLike.create = [&created]() -> RefPtr<IScriptManager> {
        ++created;
        return RefPtr<IScriptManager>(MakeRef<FakeScriptManager>(DefaultAllocator()));
    };
    registry.Register(Move(wrenLike));

    REQUIRE(registry.FindByLanguage(u8"testlang") != nullptr);
    CHECK(registry.FindByLanguage(u8"nosuch") == nullptr);
    REQUIRE(registry.FindByExtension(u8"tl") != nullptr);
    CHECK(registry.FindByExtension(u8"lua") == nullptr);

    // Extension dispatch; unknown-extension fallback only when unambiguous.
    RefPtr<IScriptManager> byFile = CreateScriptManagerForFile(u8"Scripts/game.tl");
    CHECK(byFile.Get() != nullptr);
    CHECK(created == 1);
    (void)CreateScriptManagerForLanguage(u8"testlang");
    CHECK(created == 2);
    CHECK(CreateScriptManagerForLanguage(u8"nosuch").Get() == nullptr);

    // Re-register REPLACES (idempotent by id) - no duplicate entries.
    ScriptBackendDesc again;
    again.languageId = String(u8"testlang");
    again.displayName = String(u8"TestLang2");
    again.fileExtensions.PushBack(String(u8"tl"));
    again.create = []() -> RefPtr<IScriptManager> { return {}; };
    registry.Register(Move(again));
    usize count = 0;
    for (const ScriptBackendDesc& d : registry.All())
    {
        if (d.languageId == u8"testlang") { ++count; }
    }
    CHECK(count == 1);
}

TEST_CASE("script.backend: RegisterReflectedTypes drives the two-phase contract "
          "(collect all, THEN finalize - the AngelScript requirement)")
{
    FakeScriptManager manager;
    draconic::script::RegisterReflectedTypes(manager);
    CHECK(manager.registered > 0);        // the global registry is never empty here
    CHECK(manager.finalized);
    CHECK(manager.finalizedAfterAll);     // finalize came after every RegisterType
}
