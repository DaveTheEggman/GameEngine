// Raptor Core — :core_reflection partition
//
// Reflects Core's value types (vectors, color, quaternion, transform, geometry
// primitives, Guid) so they can be introspected and bound to scripting. Plain
// value types are reflected non-intrusively via RAPTOR_REFLECT_VALUE, which
// patches each type's TypeOf<T>() in place. Call RegisterCoreTypes() once at
// startup; it also registers them in the GlobalTypeRegistry.
//
// Matrices (Mat3/Mat4) are not field-reflected: their storage is a C array
// (f32[N][N]) which can't be a property. They'll get element-accessor methods
// in a later pass.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module raptor.core:core_reflection;

import :base;
import :type_info;
import :type_registry;
import :reflection;
import :instance;
import :variant;
import :vec2;
import :vec3;
import :vec4;
import :color;
import :quat;
import :transform;
import :mat3;
import :mat4;
import :aabb;
import :plane;
import :rect;
import :guid;

namespace raptor::core
{
    // Matrices store a C array (f32[N][N]) that can't be a property, so their
    // elements are exposed via the container facility: a flat, row-major view of
    // N*N scalars (read m(r,c) as element r*N + c). No change to the math types.
    template <typename MatT, usize N>
    void RegisterMatrixElements()
    {
        static const ContainerInfo info{
            &TypeOf<f32>(),
            [](const Instance&) noexcept -> usize { return N * N; },
            [](const Instance& i, usize index) -> Variant
            { return Variant::From<f32>((&static_cast<const MatT*>(i.Pointer())->m[0][0])[index]); },
            [](const Instance& i, usize index, const Variant& value) -> Status
            {
                const f32* typed = value.TryGet<f32>();
                if (typed == nullptr) { return Status{ ErrorCode::InvalidArgument }; }
                (&static_cast<MatT*>(i.Pointer())->m[0][0])[index] = *typed;
                return Status{};
            }
        };
        const_cast<TypeInfo&>(TypeOf<MatT>()).container = &info;
    }

    RAPTOR_REFLECT_VALUE(Vec2, "raptor::core")
    {
        builder.Property<&Vec2::x>("x").Property<&Vec2::y>("y")
               .Constant("Zero", Vec2::Zero).Constant("One", Vec2::One)
               .Constant("UnitX", Vec2::UnitX).Constant("UnitY", Vec2::UnitY);
    }

    RAPTOR_REFLECT_VALUE(Vec3, "raptor::core")
    {
        builder.Property<&Vec3::x>("x").Property<&Vec3::y>("y").Property<&Vec3::z>("z")
               .Constant("Zero", Vec3::Zero).Constant("One", Vec3::One)
               .Constant("UnitX", Vec3::UnitX).Constant("UnitY", Vec3::UnitY)
               .Constant("UnitZ", Vec3::UnitZ)
               // Overloaded free functions, disambiguated by an explicit cast.
               .Method<static_cast<f32 (*)(Vec3, Vec3)>(&Dot)>("Dot")
               .Method<static_cast<f32 (*)(Vec3)>(&Length)>("Length")
               .Method<static_cast<Vec3 (*)(Vec3)>(&Normalized)>("Normalized")
               // Two same-named overloads, resolved by parameter type at lookup.
               .Method<static_cast<Vec3 (*)(Vec3, Vec3)>(&operator*)>("Mul")
               .Method<static_cast<Vec3 (*)(Vec3, f32)>(&operator*)>("Mul");
    }

    RAPTOR_REFLECT_VALUE(Vec4, "raptor::core")
    {
        builder.Property<&Vec4::x>("x").Property<&Vec4::y>("y")
               .Property<&Vec4::z>("z").Property<&Vec4::w>("w")
               .Constant("Zero", Vec4::Zero).Constant("One", Vec4::One)
               .Method<&Vec4::XYZ>("XYZ");
    }

    RAPTOR_REFLECT_VALUE(Color, "raptor::core")
    {
        builder.Property<&Color::r>("r").Property<&Color::g>("g")
               .Property<&Color::b>("b").Property<&Color::a>("a")
               .Constant("White", Color::White).Constant("Black", Color::Black)
               .Constant("Red", Color::Red).Constant("Green", Color::Green)
               .Constant("Blue", Color::Blue).Constant("Transparent", Color::Transparent)
               .Method<&Color::ToRGBA8>("ToRGBA8")      // const member
               .Method<&Color::FromRGBA8>("FromRGBA8"); // static factory
    }

    RAPTOR_REFLECT_VALUE(Quat, "raptor::core")
    {
        builder.Property<&Quat::x>("x").Property<&Quat::y>("y")
               .Property<&Quat::z>("z").Property<&Quat::w>("w")
               .Constant("Identity", Quat::Identity);
    }

    RAPTOR_REFLECT_VALUE(Transform, "raptor::core")
    {
        builder.Property<&Transform::position>("position")
               .Property<&Transform::rotation>("rotation")
               .Property<&Transform::scale>("scale")
               .Method<&Transform::ToMatrix>("ToMatrix");
    }

    // Matrices: no properties (element access is via the container facility,
    // registered separately); reflect the key static/free operations.
    RAPTOR_REFLECT_VALUE(Mat4, "raptor::core")
    {
        builder.Method<&Mat4::Identity>("Identity")
               .Method<static_cast<Mat4 (*)(const Mat4&, const Mat4&)>(&operator*)>("Mul")
               .Method<static_cast<f32 (*)(const Mat4&)>(&Determinant)>("Determinant")
               .Method<static_cast<Mat4 (*)(const Mat4&)>(&Transpose)>("Transpose")
               .Method<static_cast<Mat4 (*)(const Mat4&)>(&Inverse)>("Inverse");
    }

    RAPTOR_REFLECT_VALUE(Mat3, "raptor::core")
    {
        builder.Method<&Mat3::Identity>("Identity")
               .Method<static_cast<Mat3 (*)(const Mat3&, const Mat3&)>(&operator*)>("Mul")
               .Method<static_cast<f32 (*)(const Mat3&)>(&Determinant)>("Determinant")
               .Method<static_cast<Mat3 (*)(const Mat3&)>(&Transpose)>("Transpose")
               .Method<static_cast<Mat3 (*)(const Mat3&)>(&Inverse)>("Inverse");
    }

    RAPTOR_REFLECT_VALUE(AABB, "raptor::core")
    {
        builder.Property<&AABB::min>("min").Property<&AABB::max>("max")
               .Method<&AABB::Center>("Center").Method<&AABB::Contains>("Contains");
    }

    RAPTOR_REFLECT_VALUE(Plane, "raptor::core")
    {
        builder.Property<&Plane::normal>("normal").Property<&Plane::d>("d")
               .Method<&Plane::SignedDistance>("SignedDistance");
    }

    RAPTOR_REFLECT_VALUE(Rect, "raptor::core")
    {
        builder.Property<&Rect::x>("x").Property<&Rect::y>("y")
               .Property<&Rect::width>("width").Property<&Rect::height>("height");
    }

    RAPTOR_REFLECT_VALUE(Guid, "raptor::core")
    {
        builder.Property<&Guid::high>("high").Property<&Guid::low>("low")
               .Constant("Nil", Guid::Nil)
               .Method<&Guid::IsNil>("IsNil");
    }
}

export namespace raptor::core
{
    // Registers all Core value types for reflection (patches each TypeOf<T>())
    // and adds them to the GlobalTypeRegistry. Idempotent; call once at startup.
    void RegisterCoreTypes()
    {
        RaptorRegisterValue_Vec2();      GlobalTypeRegistry().Register(TypeOf<Vec2>());
        RaptorRegisterValue_Vec3();      GlobalTypeRegistry().Register(TypeOf<Vec3>());
        RaptorRegisterValue_Vec4();      GlobalTypeRegistry().Register(TypeOf<Vec4>());
        RaptorRegisterValue_Color();     GlobalTypeRegistry().Register(TypeOf<Color>());
        RaptorRegisterValue_Quat();      GlobalTypeRegistry().Register(TypeOf<Quat>());
        RaptorRegisterValue_Transform(); GlobalTypeRegistry().Register(TypeOf<Transform>());
        RaptorRegisterValue_Mat4();      GlobalTypeRegistry().Register(TypeOf<Mat4>());
        RaptorRegisterValue_Mat3();      GlobalTypeRegistry().Register(TypeOf<Mat3>());
        RegisterMatrixElements<Mat4, 4>();  // flat element access (after the patch above)
        RegisterMatrixElements<Mat3, 3>();
        RaptorRegisterValue_AABB();      GlobalTypeRegistry().Register(TypeOf<AABB>());
        RaptorRegisterValue_Plane();     GlobalTypeRegistry().Register(TypeOf<Plane>());
        RaptorRegisterValue_Rect();      GlobalTypeRegistry().Register(TypeOf<Rect>());
        RaptorRegisterValue_Guid();      GlobalTypeRegistry().Register(TypeOf<Guid>());
    }
}
