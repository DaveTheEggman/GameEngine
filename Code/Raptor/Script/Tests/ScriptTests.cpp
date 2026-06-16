#include <doctest/doctest.h>

#include "Core/Prelude.h"  // <new> reachability for container instantiation (GCC)
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.script;

using namespace raptor::core;
using namespace raptor::script;

// A tiny reflected object to exercise object-valued globals.
namespace
{
    class Widget : public Object
    {
        RAPTOR_OBJECT(Widget, Object)
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

RAPTOR_DEFINE_OBJECT(Widget, "raptor::script::test")

TEST_CASE("script: reflected types register with a manager")
{
    RegisterCoreTypes();

    RefPtr<MockManager> manager = MakeRef<MockManager>(DefaultAllocator());
    RegisterReflectedTypes(*manager);

    CHECK(manager->Count() >= 10u);
    CHECK(manager->Has(TypeOf<Vec3>()));
    CHECK(manager->Has(TypeOf<Guid>()));
    CHECK(manager->Has(TypeOf<Mat4>()));
}

TEST_CASE("script: context round-trips value and object globals as Variant")
{
    RefPtr<MockManager> manager = MakeRef<MockManager>(DefaultAllocator());
    RefPtr<IScriptContext> ctx = manager->CreateContext();
    REQUIRE(static_cast<bool>(ctx));

    // Value global.
    ctx->SetGlobal(u"pos", Variant::From(Vec3{ 1.0f, 2.0f, 3.0f }));
    CHECK(ctx->GetGlobal(u"pos").Get<Vec3>() == Vec3{ 1.0f, 2.0f, 3.0f });

    // Object global keeps the object alive and reports its dynamic type.
    RefPtr<Widget> widget = MakeRef<Widget>(DefaultAllocator());
    widget->id = 42;
    ctx->SetGlobal(u"w", Variant::From(widget));
    CHECK(widget->RefCount() == 2u);  // widget + the global's Variant

    Variant got = ctx->GetGlobal(u"w");
    CHECK(got.IsObject());
    CHECK(got.Type() == &Widget::StaticType());
    REQUIRE(got.AsObject<Widget>() != nullptr);
    CHECK(got.AsObject<Widget>()->id == 42);

    // Missing global -> empty Variant.
    CHECK(ctx->GetGlobal(u"missing").IsEmpty());
}

TEST_CASE("script: backend reports unsupported operations cleanly")
{
    RefPtr<MockManager> manager = MakeRef<MockManager>(DefaultAllocator());
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    CHECK(ctx->Load(u"print('hi')", u"chunk").Code() == ErrorCode::NotSupported);
    CHECK_FALSE(ctx->HasFunction(u"main"));
    CHECK(ctx->Call(u"main", Span<Variant>{}).Error() == ErrorCode::NotSupported);
}
