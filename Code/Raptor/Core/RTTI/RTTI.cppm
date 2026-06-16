// Raptor Core — :rtti partition (phase a: type identity + casting)
//
// Custom reflection foundation (replaces C++ RTTI). This first slice provides
// stable type identity (TypeInfo / TypeId), a type registry, the polymorphic
// Object root, and Cast/IsA. Properties, methods, Variant, and the registration
// builder land in later phases (see Documentation/Planning/Core.md §4.10).
//
// Registration is explicit (no static self-registration): a module calls
// RegisterTypes and hands each type's StaticType() to the registry.

module;
#include "Core/Prelude.h"

export module raptor.core:rtti;

import :base;
import :memory;
import :smart_ptr;
import :containers;
import :hash;
import :hash_map;
import :string;

export namespace raptor::core
{
    using TypeId = u64;

    struct TypeInfo
    {
        TypeId id;
        const char* name;          // unqualified, e.g. "Entity"
        const char* namespaceName; // e.g. "raptor::game"
        u32 size;
        u32 align;
        const TypeInfo* base;      // single-inheritance chain; null at the root
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
    [[nodiscard]] TypeInfo MakeTypeInfo(const char* name, const char* namespaceName, const TypeInfo* base) noexcept
    {
        return TypeInfo{ ComputeTypeId(namespaceName, name), name, namespaceName,
                         static_cast<u32>(sizeof(T)), static_cast<u32>(alignof(T)), base };
    }

    // =======================================================================
    // Type registry — explicit registration; lookup by id or qualified name.
    // =======================================================================
    class TypeRegistry
    {
    public:
        void Register(const TypeInfo& info)
        {
            if (m_byId.Contains(info.id))
            {
                return; // already registered
            }
            m_byId.InsertOrAssign(info.id, &info);
            m_all.PushBack(&info);
        }

        [[nodiscard]] const TypeInfo* FindById(TypeId id) const noexcept
        {
            const TypeInfo* const* found = m_byId.Find(id);
            return (found != nullptr) ? *found : nullptr;
        }

        [[nodiscard]] const TypeInfo* FindByName(const char* namespaceName, const char* name) const noexcept
        {
            return FindById(ComputeTypeId(namespaceName, name));
        }

        [[nodiscard]] usize Count() const noexcept { return m_all.Size(); }
        [[nodiscard]] const Array<const TypeInfo*>& All() const noexcept { return m_all; }

    private:
        HashMap<TypeId, const TypeInfo*> m_byId;
        Array<const TypeInfo*> m_all;
    };

    [[nodiscard]] TypeRegistry& GlobalTypeRegistry() noexcept
    {
        static TypeRegistry instance;
        return instance;
    }

    // =======================================================================
    // Object — polymorphic reflection root. Derives from RefCounted, so every
    // Object is held via RefPtr<Object> (§4.10).
    // =======================================================================
    class Object : public RefCounted
    {
    public:
        using Super = void;

        [[nodiscard]] virtual const TypeInfo* GetType() const noexcept { return &StaticType(); }

        [[nodiscard]] static const TypeInfo& StaticType() noexcept
        {
            static const TypeInfo info{ ComputeTypeId("raptor::core", "Object"),
                                        "Object", "raptor::core",
                                        static_cast<u32>(sizeof(Object)),
                                        static_cast<u32>(alignof(Object)),
                                        nullptr };
            return info;
        }
    };

    // =======================================================================
    // Cast / IsA — replace dynamic_cast by walking the single-inheritance chain.
    // =======================================================================
    [[nodiscard]] inline bool IsDerivedFrom(const TypeInfo* type, const TypeInfo* base) noexcept
    {
        for (const TypeInfo* t = type; t != nullptr; t = t->base)
        {
            if (t == base) { return true; }
        }
        return false;
    }

    template <typename T>
    [[nodiscard]] bool IsA(const Object* object) noexcept
    {
        return object != nullptr && IsDerivedFrom(object->GetType(), &T::StaticType());
    }

    template <typename T>
    [[nodiscard]] T* Cast(Object* object) noexcept
    {
        return IsA<T>(object) ? static_cast<T*>(object) : nullptr;
    }

    template <typename T>
    [[nodiscard]] const T* Cast(const Object* object) noexcept
    {
        return IsA<T>(object) ? static_cast<const T*>(object) : nullptr;
    }
}
