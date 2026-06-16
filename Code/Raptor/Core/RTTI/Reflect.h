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

// Defines StaticType() for `Type` in the given namespace string. The TypeInfo
// is a function-local static (created on first use); registration is separate.
#define RAPTOR_DEFINE_OBJECT(Type, Namespace)                                          \
    const ::raptor::core::TypeInfo& Type::StaticType() noexcept                         \
    {                                                                                   \
        static const ::raptor::core::TypeInfo info =                                    \
            ::raptor::core::MakeTypeInfo<Type>(#Type, Namespace, &Super::StaticType()); \
        return info;                                                                    \
    }

#endif // RAPTOR_CORE_RTTI_REFLECT_H
