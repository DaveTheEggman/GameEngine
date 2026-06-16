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
