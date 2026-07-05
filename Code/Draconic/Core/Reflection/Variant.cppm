// Draconic Core - :variant partition
//
// Variant  - an owned, type-erased value (small-buffer optimized) used for
//            property values, method args/returns. Two modes:
//              * value mode  - owns a copy of any value type T (SBO + heap).
//              * object mode - owns a RefPtr<Object> and reports the object's
//                dynamic GetType() (so scripting can wrap it as the right type).
// Instance - a borrowed { void*, TypeInfo* } target for member access. Variant
//            ALWAYS owns its value (no reference mode); see §4.10.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <cstddef>
#include <type_traits>

export module draconic.core:variant;

import :base;
import :allocator;
import :ref_counted;
import :type_info;
import :object;

namespace draconic::core::detail
{
    template <typename T>
    struct VariantOps
    {
        static void Copy(void* dst, const void* src) { Construct<T>(dst, *static_cast<const T*>(src)); }
        static void Move(void* dst, void* src) { Construct<T>(dst, draconic::core::Move(*static_cast<T*>(src))); }
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

    // Detects RefPtr<U> where U derives Object - routed to Variant's object mode.
    template <typename T>
    struct ObjectRef { static constexpr bool value = false; };
    template <typename U>
    struct ObjectRef<RefPtr<U>>
    {
        static constexpr bool value = std::is_base_of_v<Object, U>;
        using Pointee = U;
    };
}

export namespace draconic::core
{
    class Variant
    {
    public:
        Variant() noexcept = default;

        template <typename T>
        [[nodiscard]] static Variant From(T value)
        {
            if constexpr (detail::ObjectRef<T>::value)
            {
                using U = typename detail::ObjectRef<T>::Pointee;
                Variant v;
                // Dynamic type for non-null; static type as a fallback for null.
                v.m_dynamicType = (value.Get() != nullptr) ? value.Get()->GetType() : &U::StaticType();
                v.m_vtable = &detail::kVariantVTable<RefPtr<Object>>;
                void* dst = v.AllocateStorage(sizeof(RefPtr<Object>), alignof(RefPtr<Object>));
                Construct<RefPtr<Object>>(dst, RefPtr<Object>(value));
                return v;
            }
            else
            {
                Variant v;
                v.m_vtable = &detail::kVariantVTable<T>;
                void* dst = v.AllocateStorage(sizeof(T), alignof(T));
                Construct<T>(dst, Move(value));
                return v;
            }
        }

        // Wrap an object (owning). Reports the object's dynamic type.
        [[nodiscard]] static Variant FromObject(const RefPtr<Object>& object)
        {
            return From<RefPtr<Object>>(object);
        }

        Variant(const Variant& other) : m_dynamicType(other.m_dynamicType), m_vtable(other.m_vtable)
        {
            if (m_vtable != nullptr)
            {
                void* dst = AllocateStorage(m_vtable->size, m_vtable->align);
                m_vtable->copy(dst, other.Data());
            }
        }

        Variant(Variant&& other) noexcept : m_dynamicType(other.m_dynamicType), m_vtable(other.m_vtable)
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
            other.m_dynamicType = nullptr;
        }

        Variant& operator=(const Variant& other)
        {
            if (this != &other)
            {
                Reset();
                m_dynamicType = other.m_dynamicType;
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
                m_dynamicType = other.m_dynamicType;
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
                other.m_dynamicType = nullptr;
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
            m_dynamicType = nullptr;
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return m_vtable == nullptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_vtable != nullptr; }

        // True if this holds an object (RefPtr<Object>), not a plain value.
        [[nodiscard]] bool IsObject() const noexcept { return m_dynamicType != nullptr; }

        [[nodiscard]] const TypeInfo* Type() const noexcept
        {
            if (m_dynamicType != nullptr) { return m_dynamicType; } // object: dynamic type
            return m_vtable != nullptr ? m_vtable->typeInfo() : nullptr;
        }

        // Borrowed view of the held object, or null if empty / not an object.
        [[nodiscard]] Object* AsObject() const noexcept
        {
            if (m_dynamicType == nullptr) { return nullptr; }
            return static_cast<const RefPtr<Object>*>(Data())->Get();
        }

        // Borrowed, down-cast view; null if not an object or not a T.
        template <typename T>
        [[nodiscard]] T* AsObject() const noexcept { return Cast<T>(AsObject()); }

        // Address of the stored value (value mode). For the reflection/binding
        // layer to build an Instance over a value a Variant owns. For objects use
        // AsObject() instead (this returns the RefPtr storage, not the object).
        [[nodiscard]] void* ValuePointer() noexcept { return Data(); }

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
            DRACONIC_ASSERT_MSG(Is<T>(), "Variant::Get<T>() type mismatch");
            return *static_cast<T*>(Data());
        }

        template <typename T>
        [[nodiscard]] const T& Get() const noexcept
        {
            DRACONIC_ASSERT_MSG(Is<T>(), "Variant::Get<T>() type mismatch");
            return *static_cast<const T*>(Data());
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
        const TypeInfo* m_dynamicType = nullptr;  // non-null => object mode (dynamic type)
        const detail::VariantVTable* m_vtable = nullptr;
    };
}
