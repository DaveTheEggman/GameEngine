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

TEST_CASE("core-reflection: types are in the global registry by qualified name")
{
    EnsureRegistered();

    const TypeInfo* vec3 = GlobalTypeRegistry().FindByName("raptor::core", "Vec3");
    REQUIRE(vec3 != nullptr);
    CHECK(vec3 == &TypeOf<Vec3>());

    CHECK(GlobalTypeRegistry().FindByName("raptor::core", "Guid") == &TypeOf<Guid>());
    CHECK(GlobalTypeRegistry().FindByName("raptor::core", "Nope") == nullptr);
}
