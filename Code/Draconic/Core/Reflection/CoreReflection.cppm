// Draconic Core - :core_reflection partition
//
// Reflects Core's value types (vectors, color, quaternion, transform, geometry
// primitives, matrices, Guid) so they can be introspected and bound to
// scripting. Plain value types are reflected non-intrusively via
// DRACONIC_REFLECT_VALUE, which patches each type's TypeOf<T>() in place. Call
// RegisterCoreTypes() once at startup; it registers types in the
// GlobalTypeRegistry and namespace-level math constants in the
// GlobalConstantRegistry.
//
// Matrices (Float3x3/Float4x4) expose their f32[N][N] storage through the container
// facility (flat, row-major) since a C array can't be a property; their ops are
// reflected as methods.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.core:core_reflection;

import :base;
import :type_info;
import :type_registry;
import :constant_registry;
import :reflection;
import :enum_reflection;
import :math;
import :instance;
import :variant;
import :logger;
import :iserializer;
import :system;
import :float2;
import :float3;
import :float4;
import :color;
import :quaternion;
import :transform;
import :float3x3;
import :float4x4;
import :aabb;
import :plane;
import :rectangle;
import :guid;

namespace draconic::core
{
    // Matrices store a C array (f32[N][N]) that can't be a property, so their
    // elements are exposed via the container facility: a flat, row-major view of
    // N*N scalars (read m(r,c) as element r*N + c). No change to the math types.
    template <typename MatT, usize N>
    void RegisterMatrixElements()
    {
        static const ContainerInfo info{
            &TypeOf<f32>(), [](const Instance&) noexcept -> usize { return N * N; },
            [](const Instance& i, usize index) -> Variant
            {
                return Variant::From<f32>((&static_cast<const MatT*>(i.Pointer())->m[0][0])[index]);
            },
            [](const Instance& i, usize index, const Variant& value) -> Status
            {
                const f32* typed = value.TryGet<f32>();
                if (typed == nullptr)
                {
                    return Status{ErrorCode::InvalidArgument};
                }
                (&static_cast<MatT*>(i.Pointer())->m[0][0])[index] = *typed;
                return Status{};
            }};
        const_cast<TypeInfo&>(TypeOf<MatT>()).container = &info;
    }

    DRACONIC_REFLECT_VALUE(Float2, "draconic::core")
    {
        builder.Property<&Float2::x>("x")
            .Property<&Float2::y>("y")
            .Constant("Zero", Float2::Zero)
            .Constant("One", Float2::One)
            .Constant("UnitX", Float2::UnitX)
            .Constant("UnitY", Float2::UnitY)
            .Constructor()
            .Constructor<f32, f32>();
    }

    DRACONIC_REFLECT_VALUE(Float3, "draconic::core")
    {
        builder.Property<&Float3::x>("x")
            .Property<&Float3::y>("y")
            .Property<&Float3::z>("z")
            .Constant("Zero", Float3::Zero)
            .Constant("One", Float3::One)
            .Constant("UnitX", Float3::UnitX)
            .Constant("UnitY", Float3::UnitY)
            .Constant("UnitZ", Float3::UnitZ)
            // Overloaded free functions, disambiguated by an explicit cast.
            .Method<static_cast<f32 (*)(Float3, Float3)>(&Dot)>("Dot")
            .Method<static_cast<f32 (*)(Float3)>(&Length)>("Length")
            .Method<static_cast<Float3 (*)(Float3)>(&Normalized)>("Normalized")
            // Two same-named overloads, resolved by parameter type at lookup.
            .Method<static_cast<Float3 (*)(Float3, Float3)>(&operator*)>("Mul")
            .Method<static_cast<Float3 (*)(Float3, f32)>(&operator*)>("Mul")
            .Constructor()
            .Constructor<f32, f32, f32>();
    }

    DRACONIC_REFLECT_VALUE(Float4, "draconic::core")
    {
        builder.Property<&Float4::x>("x")
            .Property<&Float4::y>("y")
            .Property<&Float4::z>("z")
            .Property<&Float4::w>("w")
            .Constant("Zero", Float4::Zero)
            .Constant("One", Float4::One)
            .Method<&Float4::XYZ>("XYZ")
            .Constructor()
            .Constructor<f32, f32, f32, f32>();
    }

    DRACONIC_REFLECT_VALUE(Color, "draconic::core")
    {
        builder.Property<&Color::r>("r")
            .Property<&Color::g>("g")
            .Property<&Color::b>("b")
            .Property<&Color::a>("a")
            .Constant("White", Color::White)
            .Constant("Black", Color::Black)
            .Constant("Red", Color::Red)
            .Constant("Green", Color::Green)
            .Constant("Blue", Color::Blue)
            .Constant("Transparent", Color::Transparent)
            .Method<&Color::ToRGBA8>("ToRGBA8")     // const member
            .Method<&Color::FromRGBA8>("FromRGBA8") // static factory
            .Constructor()
            .Constructor<f32, f32, f32, f32>();
    }

    DRACONIC_REFLECT_VALUE(Quaternion, "draconic::core")
    {
        builder.Property<&Quaternion::x>("x")
            .Property<&Quaternion::y>("y")
            .Property<&Quaternion::z>("z")
            .Property<&Quaternion::w>("w")
            .Constant("Identity", Quaternion::Identity)
            .Constructor()
            .Constructor<f32, f32, f32, f32>();
    }

    DRACONIC_REFLECT_VALUE(Transform, "draconic::core")
    {
        builder.Property<&Transform::position>("position")
            .Property<&Transform::rotation>("rotation")
            .Property<&Transform::scale>("scale")
            .Method<&Transform::ToMatrix>("ToMatrix")
            .Constructor();
    }

    // Matrices: no properties (element access is via the container facility,
    // registered separately); reflect the key static/free operations.
    DRACONIC_REFLECT_VALUE(Float4x4, "draconic::core")
    {
        builder.Method<&Float4x4::Identity>("Identity")
            .Method<static_cast<Float4x4 (*)(const Float4x4&, const Float4x4&)>(&operator*)>("Mul")
            .Method<static_cast<f32 (*)(const Float4x4&)>(&Determinant)>("Determinant")
            .Method<static_cast<Float4x4 (*)(const Float4x4&)>(&Transpose)>("Transpose")
            .Method<static_cast<Float4x4 (*)(const Float4x4&)>(&Inverse)>("Inverse");
    }

    DRACONIC_REFLECT_VALUE(Float3x3, "draconic::core")
    {
        builder.Method<&Float3x3::Identity>("Identity")
            .Method<static_cast<Float3x3 (*)(const Float3x3&, const Float3x3&)>(&operator*)>("Mul")
            .Method<static_cast<f32 (*)(const Float3x3&)>(&Determinant)>("Determinant")
            .Method<static_cast<Float3x3 (*)(const Float3x3&)>(&Transpose)>("Transpose")
            .Method<static_cast<Float3x3 (*)(const Float3x3&)>(&Inverse)>("Inverse");
    }

    DRACONIC_REFLECT_VALUE(AABB, "draconic::core")
    {
        builder.Property<&AABB::min>("min")
            .Property<&AABB::max>("max")
            .Method<&AABB::Center>("Center")
            .Method<&AABB::Contains>("Contains")
            .Constructor()
            .Constructor<Float3, Float3>();
    }

    DRACONIC_REFLECT_VALUE(Plane, "draconic::core")
    {
        builder.Property<&Plane::normal>("normal")
            .Property<&Plane::d>("d")
            .Method<&Plane::SignedDistance>("SignedDistance")
            .Constructor()
            .Constructor<Float3, f32>();
    }

    DRACONIC_REFLECT_VALUE(Rectangle, "draconic::core")
    {
        builder.Property<&Rectangle::x>("x")
            .Property<&Rectangle::y>("y")
            .Property<&Rectangle::width>("width")
            .Property<&Rectangle::height>("height")
            .Constructor()
            .Constructor<f32, f32, f32, f32>();
    }

    DRACONIC_REFLECT_VALUE(Guid, "draconic::core")
    {
        builder.Property<&Guid::high>("high")
            .Property<&Guid::low>("low")
            .Constant("Nil", Guid::Nil)
            .Method<&Guid::IsNil>("IsNil")
            .Constructor()
            .Constructor<u64, u64>();
    }

    // Public Core enums (scripting-relevant). Internal enums (PropertyFlags,
    // HashMap::State, BufferedStream::Mode) are deliberately not reflected.
    DRACONIC_REFLECT_ENUM(LogLevel, "draconic::core")
    {
        builder.Value("Trace", LogLevel::Trace)
            .Value("Debug", LogLevel::Debug)
            .Value("Info", LogLevel::Info)
            .Value("Warning", LogLevel::Warning)
            .Value("Error", LogLevel::Error)
            .Value("Fatal", LogLevel::Fatal)
            .Value("Off", LogLevel::Off);
    }

    DRACONIC_REFLECT_ENUM(ErrorCode, "draconic::core")
    {
        builder.Value("Ok", ErrorCode::Ok)
            .Value("Unknown", ErrorCode::Unknown)
            .Value("InvalidArgument", ErrorCode::InvalidArgument)
            .Value("OutOfRange", ErrorCode::OutOfRange)
            .Value("OutOfMemory", ErrorCode::OutOfMemory)
            .Value("NotFound", ErrorCode::NotFound)
            .Value("NotSupported", ErrorCode::NotSupported)
            .Value("AlreadyExists", ErrorCode::AlreadyExists)
            .Value("Internal", ErrorCode::Internal);
    }

    DRACONIC_REFLECT_ENUM(SerializeMode, "draconic::core")
    {
        builder.Value("Read", SerializeMode::Read).Value("Write", SerializeMode::Write);
    }

    DRACONIC_REFLECT_ENUM(FileMode, "draconic::core")
    {
        builder.Value("Read", FileMode::Read)
            .Value("Write", FileMode::Write)
            .Value("ReadWrite", FileMode::ReadWrite)
            .Value("Append", FileMode::Append);
    }

    DRACONIC_REFLECT_ENUM(SeekOrigin, "draconic::core")
    {
        builder.Value("Begin", SeekOrigin::Begin)
            .Value("Current", SeekOrigin::Current)
            .Value("End", SeekOrigin::End);
    }
}

export namespace draconic::core
{
    // Registers all Core value types for reflection (patches each TypeOf<T>())
    // and adds them to the GlobalTypeRegistry. Idempotent; call once at startup.
    void RegisterCoreTypes()
    {
        DraconicRegisterValue_Float2();
        GlobalTypeRegistry().Register(TypeOf<Float2>());
        DraconicRegisterValue_Float3();
        GlobalTypeRegistry().Register(TypeOf<Float3>());
        DraconicRegisterValue_Float4();
        GlobalTypeRegistry().Register(TypeOf<Float4>());
        DraconicRegisterValue_Color();
        GlobalTypeRegistry().Register(TypeOf<Color>());
        DraconicRegisterValue_Quaternion();
        GlobalTypeRegistry().Register(TypeOf<Quaternion>());
        DraconicRegisterValue_Transform();
        GlobalTypeRegistry().Register(TypeOf<Transform>());
        DraconicRegisterValue_Float4x4();
        GlobalTypeRegistry().Register(TypeOf<Float4x4>());
        DraconicRegisterValue_Float3x3();
        GlobalTypeRegistry().Register(TypeOf<Float3x3>());
        RegisterMatrixElements<Float4x4, 4>(); // flat element access (after the patch above)
        RegisterMatrixElements<Float3x3, 3>();
        DraconicRegisterValue_AABB();
        GlobalTypeRegistry().Register(TypeOf<AABB>());
        DraconicRegisterValue_Plane();
        GlobalTypeRegistry().Register(TypeOf<Plane>());
        DraconicRegisterValue_Rectangle();
        GlobalTypeRegistry().Register(TypeOf<Rectangle>());
        DraconicRegisterValue_Guid();
        GlobalTypeRegistry().Register(TypeOf<Guid>());

        // Free-standing (namespace-level) math constants.
        ConstantRegistry& constants = GlobalConstantRegistry();
        const TypeInfo* f32Type = &TypeOf<f32>();
        constants.Register("draconic::core", "kPi", f32Type, Variant::From<f32>(kPi));
        constants.Register("draconic::core", "kTwoPi", f32Type, Variant::From<f32>(kTwoPi));
        constants.Register("draconic::core", "kHalfPi", f32Type, Variant::From<f32>(kHalfPi));
        constants.Register("draconic::core", "kInvPi", f32Type, Variant::From<f32>(kInvPi));
        constants.Register("draconic::core", "kDegToRad", f32Type, Variant::From<f32>(kDegToRad));
        constants.Register("draconic::core", "kRadToDeg", f32Type, Variant::From<f32>(kRadToDeg));
        constants.Register("draconic::core", "kEpsilon", f32Type, Variant::From<f32>(kEpsilon));
        constants.Register("draconic::core", "kFloatMax", f32Type, Variant::From<f32>(kFloatMax));

        // Public enums (patch TypeOf<E>() with enumerators, then register).
        DraconicRegisterEnum_LogLevel();
        GlobalTypeRegistry().Register(TypeOf<LogLevel>());
        DraconicRegisterEnum_ErrorCode();
        GlobalTypeRegistry().Register(TypeOf<ErrorCode>());
        DraconicRegisterEnum_SerializeMode();
        GlobalTypeRegistry().Register(TypeOf<SerializeMode>());
        DraconicRegisterEnum_FileMode();
        GlobalTypeRegistry().Register(TypeOf<FileMode>());
        DraconicRegisterEnum_SeekOrigin();
        GlobalTypeRegistry().Register(TypeOf<SeekOrigin>());
    }
}
