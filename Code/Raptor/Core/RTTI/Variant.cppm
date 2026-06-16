// Raptor Core — :variant partition (RTTI phase b)
//
// Variant  — an owned value with small-buffer optimization, type-erased via a
//            per-type vtable. Used for property values, method args, returns.
// Instance — a borrowed { void*, TypeInfo* } target for member access (the
//            `this` of a property/method call). Non-owning.
//
// Variant ALWAYS owns its value (no reference mode) — ownership stays a static
// property; see Documentation/Planning/Core.md §4.10.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <cstddef>

export module raptor.core:variant;

import :base;
import :memory;
import :containers;
import :rtti;

namespace raptor::core::detail
{
    template <typename T>
    struct VariantOps
    {
        static void Copy(void* dst, const void* src) { Construct<T>(dst, *static_cast<const T*>(src)); }
        static void Move(void* dst, void* src) { Construct<T>(dst, raptor::core::Move(*static_cast<T*>(src))); }
        static void Destroy(void* obj) { Destruct(static_cast<T*>(obj)); }
    };

    struct VariantVTable
    {
        void (*copy)(void* dst, const void* src);
        void (*move)(void* dst, void* src);
        void (*destroy)(void* obj);
        const TypeInfo* (*typeInfo)();
        u32 size;
        u32 align;
    };

    template <typename T>
    const TypeInfo* VariantTypeInfo() noexcept { return &TypeOf<T>(); }

    template <typename T>
    inline constexpr VariantVTable kVariantVTable{
        &VariantOps<T>::Copy, &VariantOps<T>::Move, &VariantOps<T>::Destroy,
        &VariantTypeInfo<T>, static_cast<u32>(sizeof(T)), static_cast<u32>(alignof(T))
    };
}

export namespace raptor::core
{
    class Variant
    {
    public:
        Variant() noexcept = default;

        template <typename T>
        [[nodiscard]] static Variant From(T value)
        {
            Variant v;
            v.m_vtable = &detail::kVariantVTable<T>;
            void* dst = v.AllocateStorage(sizeof(T), alignof(T));
            Construct<T>(dst, Move(value));
            return v;
        }

        Variant(const Variant& other) : m_vtable(other.m_vtable)
        {
            if (m_vtable != nullptr)
            {
                void* dst = AllocateStorage(m_vtable->size, m_vtable->align);
                m_vtable->copy(dst, other.Data());
            }
        }

        Variant(Variant&& other) noexcept : m_vtable(other.m_vtable)
        {
            if (m_vtable != nullptr)
            {
                if (other.m_isHeap)
                {
                    m_isHeap = true;
                    m_storage.heap = other.m_storage.heap; // steal
                }
                else
                {
                    void* dst = AllocateStorage(m_vtable->size, m_vtable->align);
                    m_vtable->move(dst, other.Data());
                    m_vtable->destroy(other.Data());
                }
            }
            other.m_vtable = nullptr;
            other.m_isHeap = false;
        }

        Variant& operator=(const Variant& other)
        {
            if (this != &other)
            {
                Reset();
                m_vtable = other.m_vtable;
                if (m_vtable != nullptr)
                {
                    void* dst = AllocateStorage(m_vtable->size, m_vtable->align);
                    m_vtable->copy(dst, other.Data());
                }
            }
            return *this;
        }

        Variant& operator=(Variant&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_vtable = other.m_vtable;
                if (m_vtable != nullptr)
                {
                    if (other.m_isHeap)
                    {
                        m_isHeap = true;
                        m_storage.heap = other.m_storage.heap;
                    }
                    else
                    {
                        void* dst = AllocateStorage(m_vtable->size, m_vtable->align);
                        m_vtable->move(dst, other.Data());
                        m_vtable->destroy(other.Data());
                    }
                }
                other.m_vtable = nullptr;
                other.m_isHeap = false;
            }
            return *this;
        }

        ~Variant() { Reset(); }

        void Reset() noexcept
        {
            if (m_vtable != nullptr)
            {
                m_vtable->destroy(Data());
                if (m_isHeap)
                {
                    DefaultAllocator().Free(m_storage.heap);
                }
            }
            m_vtable = nullptr;
            m_isHeap = false;
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_vtable == nullptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_vtable != nullptr; }

        [[nodiscard]] const TypeInfo* Type() const noexcept
        {
            return m_vtable != nullptr ? m_vtable->typeInfo() : nullptr;
        }

        template <typename T>
        [[nodiscard]] bool Is() const noexcept { return m_vtable == &detail::kVariantVTable<T>; }

        template <typename T>
        [[nodiscard]] T* TryGet() noexcept
        {
            return Is<T>() ? static_cast<T*>(Data()) : nullptr;
        }

        template <typename T>
        [[nodiscard]] const T* TryGet() const noexcept
        {
            return Is<T>() ? static_cast<const T*>(Data()) : nullptr;
        }

        template <typename T>
        [[nodiscard]] T& Get() noexcept
        {
            RAPTOR_ASSERT_MSG(Is<T>(), "Variant::Get<T>() type mismatch");
            return *static_cast<T*>(Data());
        }

    private:
        static constexpr usize kInlineSize = 3 * sizeof(void*);
        static constexpr usize kInlineAlign = alignof(std::max_align_t);

        void* AllocateStorage(usize size, usize align)
        {
            if (size <= kInlineSize && align <= kInlineAlign)
            {
                m_isHeap = false;
                return &m_storage.inlineBytes;
            }
            m_isHeap = true;
            m_storage.heap = DefaultAllocator().Allocate(size, align);
            return m_storage.heap;
        }

        [[nodiscard]] void* Data() noexcept
        {
            return m_isHeap ? m_storage.heap : static_cast<void*>(&m_storage.inlineBytes);
        }
        [[nodiscard]] const void* Data() const noexcept
        {
            return m_isHeap ? m_storage.heap : static_cast<const void*>(&m_storage.inlineBytes);
        }

        union Storage
        {
            alignas(kInlineAlign) unsigned char inlineBytes[kInlineSize];
            void* heap;
        };

        Storage m_storage{};
        bool m_isHeap = false;
        const detail::VariantVTable* m_vtable = nullptr;
    };

    // =======================================================================
    // Instance — a borrowed, type-erased pointer to a live object.
    // =======================================================================
    class Instance
    {
    public:
        Instance() noexcept = default;
        Instance(void* pointer, const TypeInfo* type) noexcept : m_ptr(pointer), m_type(type) {}

        template <typename T>
        [[nodiscard]] static Instance From(T* pointer) noexcept
        {
            return Instance{ pointer, &TypeOf<T>() };
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_ptr == nullptr; }
        [[nodiscard]] void* Pointer() const noexcept { return m_ptr; }
        [[nodiscard]] const TypeInfo* Type() const noexcept { return m_type; }

        template <typename T>
        [[nodiscard]] T* TryGet() const noexcept
        {
            return (m_type == &TypeOf<T>()) ? static_cast<T*>(m_ptr) : nullptr;
        }

    private:
        void* m_ptr = nullptr;
        const TypeInfo* m_type = nullptr;
    };
}

// ---------------------------------------------------------------------------
// Properties (RTTI phase c)
// ---------------------------------------------------------------------------
namespace raptor::core::detail
{
    template <typename>
    struct MemberTraits;
    template <typename C, typename M>
    struct MemberTraits<M C::*>
    {
        using Class = C;
        using Member = M;
    };

    template <typename T, typename M, auto Member>
    Variant PropertyGet(const Instance& instance)
    {
        const T* object = static_cast<const T*>(instance.Pointer());
        return Variant::From<M>(object->*Member);
    }

    template <typename T, typename M, auto Member>
    Status PropertySet(const Instance& instance, const Variant& value)
    {
        const M* typed = value.TryGet<M>();
        if (typed == nullptr)
        {
            return Status{ ErrorCode::InvalidArgument };
        }
        T* object = static_cast<T*>(instance.Pointer());
        object->*Member = *typed;
        return Status{};
    }

    [[nodiscard]] inline bool CStringEquals(const char* a, const char* b) noexcept
    {
        usize i = 0;
        while (a[i] != '\0' && a[i] == b[i]) { ++i; }
        return a[i] == b[i];
    }
}

export namespace raptor::core
{
    enum class PropertyFlags : u32
    {
        None = 0,
        ReadOnly = 1u << 0,
    };

    struct PropertyInfo
    {
        const char* name;
        const TypeInfo* type;
        PropertyFlags flags;
        Variant (*get)(const Instance&);
        Status (*set)(const Instance&, const Variant&);
    };

    [[nodiscard]] inline Variant GetProperty(const PropertyInfo& property, const Instance& instance)
    {
        return property.get(instance);
    }

    [[nodiscard]] inline Status SetProperty(const PropertyInfo& property, const Instance& instance, const Variant& value)
    {
        return property.set(instance, value);
    }

    // Properties declared directly on `type` (not inherited).
    [[nodiscard]] inline Span<const PropertyInfo> Properties(const TypeInfo& type) noexcept
    {
        return Span<const PropertyInfo>{ type.properties, type.propertyCount };
    }

    // Searches `type` and its base chain for a property by name.
    [[nodiscard]] inline const PropertyInfo* FindProperty(const TypeInfo& type, const char* name) noexcept
    {
        for (const TypeInfo* t = &type; t != nullptr; t = t->base)
        {
            for (u32 i = 0; i < t->propertyCount; ++i)
            {
                if (detail::CStringEquals(t->properties[i].name, name))
                {
                    return &t->properties[i];
                }
            }
        }
        return nullptr;
    }

    // Holds a type's TypeInfo together with the property array it points into.
    // Stored as a single static (see RAPTOR_REFLECT); Array's move preserves the
    // buffer address, so TypeInfo::properties stays valid.
    struct TypeData
    {
        Array<PropertyInfo> properties;
        TypeInfo info{};
    };

    template <typename T>
    class TypeBuilder
    {
    public:
        TypeBuilder(const char* name, const char* namespaceName, const TypeInfo* base) noexcept
            : m_name(name), m_namespace(namespaceName), m_base(base) {}

        template <auto Member>
        TypeBuilder& Property(const char* name, PropertyFlags flags = PropertyFlags::None)
        {
            using M = typename detail::MemberTraits<decltype(Member)>::Member;
            m_data.properties.PushBack(PropertyInfo{
                name, &TypeOf<M>(), flags,
                &detail::PropertyGet<T, M, Member>,
                &detail::PropertySet<T, M, Member> });
            return *this;
        }

        [[nodiscard]] TypeData Build()
        {
            m_data.info = MakeTypeInfo<T>(m_name, m_namespace, m_base);
            m_data.info.properties = m_data.properties.Data();
            m_data.info.propertyCount = static_cast<u32>(m_data.properties.Size());
            return Move(m_data);
        }

    private:
        const char* m_name;
        const char* m_namespace;
        const TypeInfo* m_base;
        TypeData m_data;
    };
}
