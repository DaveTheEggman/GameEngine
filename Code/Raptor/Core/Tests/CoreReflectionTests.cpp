#include <doctest/doctest.h>

#include "Core/Prelude.h"  // brings <new> into reach for container instantiation (GCC)

import raptor.core;

using namespace raptor::core;

// Reflection of Core value types is registered explicitly; idempotent, so each
// test can ensure it.
namespace
{
    void EnsureRegistered() { RegisterCoreTypes(); }
}

TEST_CASE("core-reflection: value-type properties are reflected")
{
    EnsureRegistered();

    const TypeInfo& vec3 = TypeOf<Vec3>();
    CHECK(Properties(vec3).Size() == 3u);

    const PropertyInfo* x = FindProperty(vec3, "x");
    const PropertyInfo* y = FindProperty(vec3, "y");
    const PropertyInfo* z = FindProperty(vec3, "z");
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    REQUIRE(z != nullptr);
    CHECK(x->type == &TypeOf<f32>());
    CHECK(FindProperty(vec3, "w") == nullptr);
}

TEST_CASE("core-reflection: get / set a property through an Instance")
{
    EnsureRegistered();

    Vec3 v{ 1.0f, 2.0f, 3.0f };
    Instance inst = Instance::From(&v);

    const PropertyInfo* y = FindProperty(TypeOf<Vec3>(), "y");
    REQUIRE(y != nullptr);
    CHECK(GetProperty(*y, inst).Get<f32>() == 2.0f);

    CHECK(SetProperty(*y, inst, Variant::From(9.0f)).IsOk());
    CHECK(v.y == 9.0f);
    CHECK(GetProperty(*y, inst).Get<f32>() == 9.0f);

    // Wrong-typed set is rejected.
    CHECK_FALSE(SetProperty(*y, inst, Variant::From(7)).IsOk());
}

TEST_CASE("core-reflection: nested value-type properties (Transform)")
{
    EnsureRegistered();

    const TypeInfo& transform = TypeOf<Transform>();
    CHECK(Properties(transform).Size() == 3u);

    const PropertyInfo* position = FindProperty(transform, "position");
    const PropertyInfo* rotation = FindProperty(transform, "rotation");
    REQUIRE(position != nullptr);
    REQUIRE(rotation != nullptr);
    CHECK(position->type == &TypeOf<Vec3>());
    CHECK(rotation->type == &TypeOf<Quat>());

    Transform t;
    Instance inst = Instance::From(&t);
    CHECK(SetProperty(*position, inst, Variant::From(Vec3{ 4.0f, 5.0f, 6.0f })).IsOk());
    CHECK(t.position == Vec3{ 4.0f, 5.0f, 6.0f });
    CHECK(GetProperty(*position, inst).Get<Vec3>() == Vec3{ 4.0f, 5.0f, 6.0f });
}

TEST_CASE("core-reflection: named constants are reflected")
{
    EnsureRegistered();

    const TypeInfo& vec3 = TypeOf<Vec3>();
    CHECK(Constants(vec3).Size() == 5u);

    const ConstantInfo* zero = FindConstant(vec3, "Zero");
    const ConstantInfo* unitY = FindConstant(vec3, "UnitY");
    REQUIRE(zero != nullptr);
    REQUIRE(unitY != nullptr);
    CHECK(zero->type == &TypeOf<Vec3>());
    CHECK(zero->value.Get<Vec3>() == Vec3::Zero);
    CHECK(unitY->value.Get<Vec3>() == Vec3::UnitY);
    CHECK(FindConstant(vec3, "Nope") == nullptr);

    // Constants on other types.
    CHECK(NearlyEqual(FindConstant(TypeOf<Quat>(), "Identity")->value.Get<Quat>(), Quat::Identity));
    CHECK(FindConstant(TypeOf<Color>(), "Red")->value.Get<Color>() == Color::Red);
    CHECK(FindConstant(TypeOf<Guid>(), "Nil")->value.Get<Guid>() == Guid::Nil);
}

TEST_CASE("core-reflection: member, const, and static methods invoke")
{
    EnsureRegistered();

    // const member returning a value type
    Vec4 v{ 1.0f, 2.0f, 3.0f, 4.0f };
    Instance vi = Instance::From(&v);
    const MethodInfo* xyz = FindMethod(TypeOf<Vec4>(), "XYZ");
    REQUIRE(xyz != nullptr);
    CHECK_FALSE(xyz->isStatic);
    CHECK(xyz->isConst);
    CHECK(InvokeMethod(*xyz, vi, Span<Variant>{}).Value().Get<Vec3>() == Vec3{ 1.0f, 2.0f, 3.0f });

    // const member returning a scalar
    Color white = Color::White;
    Instance wi = Instance::From(&white);
    const MethodInfo* toRGBA = FindMethod(TypeOf<Color>(), "ToRGBA8");
    REQUIRE(toRGBA != nullptr);
    CHECK(InvokeMethod(*toRGBA, wi, Span<Variant>{}).Value().Get<u32>() == 0xFFFFFFFFu);

    // static factory
    const MethodInfo* fromRGBA = FindMethod(TypeOf<Color>(), "FromRGBA8");
    REQUIRE(fromRGBA != nullptr);
    CHECK(fromRGBA->isStatic);
    Variant fromArgs[] = { Variant::From<u32>(0xFFFFFFFFu) };
    CHECK(InvokeStatic(*fromRGBA, Span<Variant>{ fromArgs, 1 }).Value().Get<Color>() == Color::White);

    // member taking an argument
    Guid nil = Guid::Nil;
    Instance gi = Instance::From(&nil);
    CHECK(InvokeMethod(*FindMethod(TypeOf<Guid>(), "IsNil"), gi, Span<Variant>{}).Value().Get<bool>());
}

TEST_CASE("core-reflection: overloaded free functions reflect (disambiguated by cast)")
{
    EnsureRegistered();

    // Dot/Length were overloaded free functions; reflected as static methods.
    const MethodInfo* dot = FindMethod(TypeOf<Vec3>(), "Dot");
    REQUIRE(dot != nullptr);
    CHECK(dot->isStatic);
    CHECK(dot->returnType == &TypeOf<f32>());
    REQUIRE(dot->paramCount == 2u);
    CHECK(dot->params[0].type == &TypeOf<Vec3>());
    Variant dotArgs[] = { Variant::From(Vec3{ 1.0f, 2.0f, 3.0f }), Variant::From(Vec3{ 4.0f, 5.0f, 6.0f }) };
    CHECK(InvokeStatic(*dot, Span<Variant>{ dotArgs, 2 }).Value().Get<f32>() == 32.0f);
}

TEST_CASE("core-reflection: same-named overloads resolved by parameter type")
{
    EnsureRegistered();

    const Vec3 a{ 2.0f, 3.0f, 4.0f };

    // Vec3 * f32
    const TypeInfo* const scalarSig[] = { &TypeOf<Vec3>(), &TypeOf<f32>() };
    const MethodInfo* mulScalar = FindMethod(TypeOf<Vec3>(), "Mul",
        Span<const TypeInfo* const>{ scalarSig, 2 });
    REQUIRE(mulScalar != nullptr);

    // Vec3 * Vec3
    const TypeInfo* const vecSig[] = { &TypeOf<Vec3>(), &TypeOf<Vec3>() };
    const MethodInfo* mulVec = FindMethod(TypeOf<Vec3>(), "Mul",
        Span<const TypeInfo* const>{ vecSig, 2 });
    REQUIRE(mulVec != nullptr);

    CHECK(mulScalar != mulVec);  // distinct overloads selected by signature

    Variant scalarArgs[] = { Variant::From(a), Variant::From(2.0f) };
    CHECK(InvokeStatic(*mulScalar, Span<Variant>{ scalarArgs, 2 }).Value().Get<Vec3>()
          == Vec3{ 4.0f, 6.0f, 8.0f });

    Variant vecArgs[] = { Variant::From(a), Variant::From(Vec3{ 1.0f, 2.0f, 3.0f }) };
    CHECK(InvokeStatic(*mulVec, Span<Variant>{ vecArgs, 2 }).Value().Get<Vec3>()
          == Vec3{ 2.0f, 6.0f, 12.0f });

    // Name-only lookup still returns the first overload.
    CHECK(FindMethod(TypeOf<Vec3>(), "Mul") != nullptr);
}

TEST_CASE("core-reflection: types are in the global registry by qualified name")
{
    EnsureRegistered();

    const TypeInfo* vec3 = GlobalTypeRegistry().FindByName("raptor::core", "Vec3");
    REQUIRE(vec3 != nullptr);
    CHECK(vec3 == &TypeOf<Vec3>());

    CHECK(GlobalTypeRegistry().FindByName("raptor::core", "Guid") == &TypeOf<Guid>());
    CHECK(GlobalTypeRegistry().FindByName("raptor::core", "Nope") == nullptr);
}
