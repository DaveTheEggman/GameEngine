// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Core - :type_info partition
//
// The type-system foundation: stable type identity (TypeId / TypeInfo),
// ComputeTypeId, and TypeOf<T>. Registry, Object, casting, and the reflection
// runtime build on this.

module;
#include "Core/Prelude.h"

export module foundation.core:type_info;

import :base;
import :hash;
import :string;

export namespace foundation::core
{
    using TypeId = u64;

    struct PropertyInfo;    // fully defined in :reflection
    struct MethodInfo;      // fully defined in :reflection
    struct Attribute;       // fully defined in :reflection
    struct ContainerInfo;   // fully defined in :reflection
    struct ConstantInfo;    // fully defined in :reflection (named static values)
    struct ConstructorInfo; // fully defined in :reflection

    struct EnumValue
    {
        const char* name;
        i64 value;
    };

    struct TypeInfo
    {
        TypeId id;
        const char* name;          // unqualified, e.g. "Entity"
        const char* namespaceName; // e.g. "rtti::game"
        u32 size;
        u32 align;
        const TypeInfo* base;                     // single-inheritance chain; null at the root
        const PropertyInfo* properties = nullptr; // declared in this type (not inherited)
        u32 propertyCount = 0;
        const MethodInfo* methods = nullptr;
        u32 methodCount = 0;
        const EnumValue* enumerators = nullptr; // populated for reflected enums
        u32 enumeratorCount = 0;
        const Attribute* attributes = nullptr;
        u32 attributeCount = 0;
        const ContainerInfo* container = nullptr; // non-null for reflected containers
        const ConstantInfo* constants = nullptr;  // named static values (e.g. Float3::Zero)
        u32 constantCount = 0;
        const ConstructorInfo* constructors = nullptr; // reflected constructors (overloads)
        u32 constructorCount = 0;
        // DATA version for serialization migration (Traktor-style): bump when the type's
        // serialized layout changes; Serialize bodies branch on ar.Version() for old data.
        // 0 = never versioned. Set via RTTI_DEFINE_OBJECT_VERSIONED or
        // TypeBuilder::DataVersion.
        u32 dataVersion = 0;
    };

    // Stable 64-bit identity from the fully-qualified name.
    [[nodiscard]] inline TypeId ComputeTypeId(const char* namespaceName, const char* name) noexcept
    {
        u64 hash = HashBytes(namespaceName, CStringLength(namespaceName));
        hash = HashBytes("::", 2, hash);
        hash = HashBytes(name, CStringLength(name), hash);
        return hash;
    }

    template <typename T>
    [[nodiscard]] TypeInfo MakeTypeInfo(const char* name, const char* namespaceName,
                                        const TypeInfo* base, u32 dataVersion = 0) noexcept
    {
        TypeInfo info{
            ComputeTypeId(namespaceName, name), name, namespaceName, static_cast<u32>(sizeof(T)),
            static_cast<u32>(alignof(T)),       base};
        info.dataVersion = dataVersion;
        return info;
    }

    namespace detail
    {
        // FNV-1a over the compiler's signature string for this instantiation: distinct
        // per T, and - unlike an address - identical in every translation unit AND every
        // shared library produced by one toolchain. This is the RUNTIME identity for
        // unregistered value types; it is never serialized (disk formats go through
        // authored names). See Documentation/Specs/shared-libraries.md.
        template <typename T>
        [[nodiscard]] consteval TypeId SignatureTypeId() noexcept
        {
#if COMPILER_MSVC
            const char* s = __FUNCSIG__;
#else
            const char* s = __PRETTY_FUNCTION__;
#endif
            u64 hash = 14695981039346656037ull;
            for (; *s != '\0'; ++s)
            {
                hash = (hash ^ static_cast<u64>(static_cast<unsigned char>(*s))) *
                       1099511628211ull;
            }
            return hash == 0 ? 1 : hash;
        }
    }

    // Lazily-created TypeInfo for any value type not covered by REFLECT_VALUE /
    // REFLECT_ENUM; used by Variant/Instance for type checks. Object-derived types
    // should prefer their StaticType() instead. The id is the compile-time signature
    // hash above, so identity survives shared-library boundaries where the old
    // address-derived id (and this static's address itself) do not. Registration
    // (REFLECT_VALUE/EnumBuilder) still overwrites id + name with the authored ones.
    template <typename T>
    [[nodiscard]] const TypeInfo& TypeOf() noexcept
    {
        static TypeInfo info = []() noexcept
        {
            TypeInfo t = MakeTypeInfo<T>("<value>", "", nullptr);
            t.id = detail::SignatureTypeId<T>();
            return t;
        }();
        return info;
    }
}
