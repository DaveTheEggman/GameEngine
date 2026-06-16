// Raptor Core — RTTI declaration macros (classic header).
//
// Use inside an Object-derived class to wire up type identity, then define the
// type once in a .cpp. Registration stays explicit — call
// GlobalTypeRegistry().Register(Type::StaticType()) from a RegisterTypes()
// function (see Documentation/Planning/Core.md §4.10).
//
//   // header
//   class Entity : public Object { RAPTOR_OBJECT(Entity, Object) public: ... };
//   // source
//   RAPTOR_DEFINE_OBJECT(Entity, "raptor::game")

#ifndef RAPTOR_CORE_RTTI_REFLECT_H
#define RAPTOR_CORE_RTTI_REFLECT_H

#include "Core/Prelude.h"

// Declares static/virtual type accessors. Leaves access as `public:`.
#define RAPTOR_OBJECT(Type, BaseType)                                                  \
public:                                                                                 \
    using Super = BaseType;                                                             \
    static const ::raptor::core::TypeInfo& StaticType() noexcept;                       \
    const ::raptor::core::TypeInfo* GetType() const noexcept override                   \
    {                                                                                   \
        return &StaticType();                                                           \
    }

// Defines StaticType() for a Type with no reflected properties.
#define RAPTOR_DEFINE_OBJECT(Type, Namespace)                                          \
    const ::raptor::core::TypeInfo& Type::StaticType() noexcept                         \
    {                                                                                   \
        static const ::raptor::core::TypeInfo info =                                    \
            ::raptor::core::MakeTypeInfo<Type>(#Type, Namespace, &Super::StaticType()); \
        return info;                                                                    \
    }

// Defines StaticType() with a reflection body that configures `builder`, e.g.:
//   RAPTOR_REFLECT(Entity, "raptor::game")
//   {
//       builder.Property<&Entity::name>("name");
//   }
#define RAPTOR_REFLECT(Type, Namespace)                                                 \
    static void RaptorReflect_##Type(::raptor::core::TypeBuilder<Type>& builder);        \
    const ::raptor::core::TypeInfo& Type::StaticType() noexcept                          \
    {                                                                                    \
        static ::raptor::core::TypeData raptorTypeData = []() {                          \
            ::raptor::core::TypeBuilder<Type> builder(#Type, Namespace, &Super::StaticType()); \
            RaptorReflect_##Type(builder);                                               \
            return builder.Build();                                                      \
        }();                                                                             \
        return raptorTypeData.info;                                                      \
    }                                                                                    \
    static void RaptorReflect_##Type([[maybe_unused]] ::raptor::core::TypeBuilder<Type>& builder)

// Reflects an enum's named values. Defines a registration function
// RaptorRegisterEnum_<EnumType>() to be called explicitly at startup, e.g.:
//   RAPTOR_REFLECT_ENUM(Color, "raptor::game")
//   {
//       builder.Value("Red", Color::Red);
//       builder.Value("Green", Color::Green);
//   }
//   // later: RaptorRegisterEnum_Color();
#define RAPTOR_REFLECT_ENUM(EnumType, Namespace)                                        \
    static void RaptorEnumBody_##EnumType(::raptor::core::EnumBuilder<EnumType>&);       \
    void RaptorRegisterEnum_##EnumType()                                                 \
    {                                                                                    \
        ::raptor::core::EnumBuilder<EnumType> builder(#EnumType, Namespace);             \
        RaptorEnumBody_##EnumType(builder);                                              \
        builder.Build();                                                                 \
    }                                                                                    \
    static void RaptorEnumBody_##EnumType([[maybe_unused]] ::raptor::core::EnumBuilder<EnumType>& builder)

// Reflects a non-Object value type (plain struct) non-intrusively: builds its
// properties/methods with a TypeBuilder and patches the type's TypeOf<T>() in
// place (so it gains a qualified name/id + members without an intrusive
// StaticType()). Defines RaptorRegisterValue_<Type>() to call once at startup,
// e.g.:
//   RAPTOR_REFLECT_VALUE(Vec3, "raptor::core")
//   {
//       builder.Property<&Vec3::x>("x").Property<&Vec3::y>("y").Property<&Vec3::z>("z");
//   }
//   // later: RaptorRegisterValue_Vec3();
#define RAPTOR_REFLECT_VALUE(Type, Namespace)                                          \
    static void RaptorReflectValue_##Type(::raptor::core::TypeBuilder<Type>& builder);   \
    void RaptorRegisterValue_##Type()                                                    \
    {                                                                                     \
        static ::raptor::core::TypeData raptorTypeData = []() {                          \
            ::raptor::core::TypeBuilder<Type> builder(#Type, Namespace, nullptr);        \
            RaptorReflectValue_##Type(builder);                                          \
            return builder.Build();                                                       \
        }();                                                                              \
        const_cast<::raptor::core::TypeInfo&>(::raptor::core::TypeOf<Type>()) =          \
            raptorTypeData.info;                                                          \
    }                                                                                     \
    static void RaptorReflectValue_##Type([[maybe_unused]] ::raptor::core::TypeBuilder<Type>& builder)

#endif // RAPTOR_CORE_RTTI_REFLECT_H
