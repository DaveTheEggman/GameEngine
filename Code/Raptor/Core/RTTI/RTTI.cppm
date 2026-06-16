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

    struct PropertyInfo; // fully defined in :variant (phase c)
    struct MethodInfo;   // fully defined in :variant (phase d)
    struct Attribute;    // fully defined in :variant (phase e)
    struct ContainerInfo; // fully defined in :variant (phase f)

    struct EnumValue
    {
        const char* name;
        i64 value;
    };

    struct TypeInfo
    {
        TypeId id;
        const char* name;          // unqualified, e.g. "Entity"
        const char* namespaceName; // e.g. "raptor::game"
        u32 size;
        u32 align;
        const TypeInfo* base;       // single-inheritance chain; null at the root
        const PropertyInfo* properties = nullptr; // declared in this type (not inherited)
        u32 propertyCount = 0;
        const MethodInfo* methods = nullptr;
        u32 methodCount = 0;
        const EnumValue* enumerators = nullptr; // populated for reflected enums
        u32 enumeratorCount = 0;
        const Attribute* attributes = nullptr;
        u32 attributeCount = 0;
        const ContainerInfo* container = nullptr; // non-null for reflected containers
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

    // Lazily-created TypeInfo for any value type. Identity is the returned
    // object's address (process-stable); used by Variant/Instance for type
    // checks. Object-derived types should prefer their StaticType() instead.
    // (A nice name / stable hashed id for value types comes in a later phase.)
    template <typename T>
    [[nodiscard]] const TypeInfo& TypeOf() noexcept
    {
        static TypeInfo info = MakeTypeInfo<T>("<value>", "", nullptr);
        static const bool initialized = []() noexcept
        {
            info.id = static_cast<TypeId>(reinterpret_cast<uptr>(&info));
            return true;
        }();
        (void)initialized;
        return info;
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

    // =======================================================================
    // Enum reflection (phase e). EnumBuilder patches the enum's TypeOf<E>()
    // TypeInfo in place (adding enumerators + a qualified name/id), so property
    // types that point at it gain the enumerator list regardless of order.
    // =======================================================================
    namespace detail
    {
        template <typename E>
        [[nodiscard]] Array<EnumValue>& EnumStorage() noexcept
        {
            static Array<EnumValue> values;
            return values;
        }

        [[nodiscard]] inline bool NameEquals(const char* a, const char* b) noexcept
        {
            usize i = 0;
            while (a[i] != '\0' && a[i] == b[i]) { ++i; }
            return a[i] == b[i];
        }
    }

    template <typename E>
    class EnumBuilder
    {
    public:
        EnumBuilder(const char* name, const char* namespaceName) noexcept
            : m_name(name), m_namespace(namespaceName) {}

        EnumBuilder& Value(const char* name, E value)
        {
            m_values.PushBack(EnumValue{ name, static_cast<i64>(value) });
            return *this;
        }

        void Build()
        {
            Array<EnumValue>& storage = detail::EnumStorage<E>();
            storage = Move(m_values);

            TypeInfo& info = const_cast<TypeInfo&>(TypeOf<E>());
            info.name = m_name;
            info.namespaceName = m_namespace;
            info.id = ComputeTypeId(m_namespace, m_name);
            info.enumerators = storage.Data();
            info.enumeratorCount = static_cast<u32>(storage.Size());
        }

    private:
        const char* m_name;
        const char* m_namespace;
        Array<EnumValue> m_values;
    };

    [[nodiscard]] inline bool IsEnum(const TypeInfo& type) noexcept { return type.enumeratorCount > 0; }

    [[nodiscard]] inline Span<const EnumValue> Enumerators(const TypeInfo& type) noexcept
    {
        return Span<const EnumValue>{ type.enumerators, type.enumeratorCount };
    }

    // Name for an enum value, or nullptr if not found.
    [[nodiscard]] inline const char* EnumValueName(const TypeInfo& type, i64 value) noexcept
    {
        for (u32 i = 0; i < type.enumeratorCount; ++i)
        {
            if (type.enumerators[i].value == value) { return type.enumerators[i].name; }
        }
        return nullptr;
    }

    // Looks up the integer value for an enumerator name; false if not found.
    [[nodiscard]] inline bool EnumValueByName(const TypeInfo& type, const char* name, i64& outValue) noexcept
    {
        for (u32 i = 0; i < type.enumeratorCount; ++i)
        {
            if (detail::NameEquals(type.enumerators[i].name, name))
            {
                outValue = type.enumerators[i].value;
                return true;
            }
        }
        return false;
    }
}
