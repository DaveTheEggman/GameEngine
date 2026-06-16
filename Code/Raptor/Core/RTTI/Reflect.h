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

#endif // RAPTOR_CORE_RTTI_REFLECT_H
