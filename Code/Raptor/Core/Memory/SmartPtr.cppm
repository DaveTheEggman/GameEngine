// Raptor Core — :smart_ptr partition
//
// Ownership smart pointers (no std:: equivalents). Allocation is explicit: the
// owning allocator is passed at creation, matching the engine-wide policy.
//
//   UniquePtr<T> — sole ownership.
//   RefCounted   — intrusive strong-ref base (Object will derive from it).
//   RefPtr<T>    — intrusive shared ownership of a RefCounted-derived type.
//
// NOTE: WeakRefPtr is deferred. Correct intrusive weak refs require splitting
// "destroy the object" (strong -> 0) from "free the storage" (weak -> 0); see
// Documentation/Planning/Core.md §4.2 / §4.10. The current RefCounted is
// strong-only and frees immediately at strong -> 0.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <atomic>
#include <type_traits>

export module raptor.core:smart_ptr;

import :base;
import :memory;

export namespace raptor::core
{
    template <typename T>
    class RefPtr;

    template <typename T, typename... Args>
    [[nodiscard]] RefPtr<T> MakeRef(IAllocator& allocator, Args&&... args);

    // =======================================================================
    // RefCounted — intrusive strong reference count.
    //   Objects must be heap-allocated through MakeRef (it records the owning
    //   allocator and frees the object when the count reaches zero).
    // =======================================================================
    class RefCounted
    {
    public:
        void AddRef() const noexcept
        {
            m_strong.fetch_add(1, std::memory_order_relaxed);
        }

        void Release() const noexcept
        {
            if (m_strong.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                IAllocator* allocator = m_allocator;
                this->~RefCounted();                                  // virtual -> derived dtor
                if (allocator != nullptr)
                {
                    allocator->Free(const_cast<RefCounted*>(this));
                }
            }
        }

        [[nodiscard]] u32 RefCount() const noexcept
        {
            return m_strong.load(std::memory_order_relaxed);
        }

    protected:
        RefCounted() noexcept = default;
        virtual ~RefCounted() = default;

        RefCounted(const RefCounted&) = delete;
        RefCounted& operator=(const RefCounted&) = delete;

    private:
        template <typename U, typename... Args>
        friend RefPtr<U> MakeRef(IAllocator&, Args&&...);

        void SetOwningAllocator(IAllocator* allocator) noexcept { m_allocator = allocator; }

        mutable std::atomic<u32> m_strong{ 0 };
        IAllocator* m_allocator = nullptr;
    };

    // =======================================================================
    // RefPtr — intrusive shared pointer.
    // =======================================================================
    template <typename T>
    class RefPtr
    {
    public:
        RefPtr() noexcept = default;
        RefPtr(decltype(nullptr)) noexcept {}

        explicit RefPtr(T* pointer) noexcept : m_ptr(pointer)
        {
            if (m_ptr != nullptr) { m_ptr->AddRef(); }
        }

        RefPtr(const RefPtr& other) noexcept : m_ptr(other.m_ptr)
        {
            if (m_ptr != nullptr) { m_ptr->AddRef(); }
        }

        RefPtr(RefPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

        // Upcast from a derived RefPtr<U>.
        template <typename U>
            requires std::is_convertible_v<U*, T*>
        RefPtr(const RefPtr<U>& other) noexcept : m_ptr(other.Get())
        {
            if (m_ptr != nullptr) { m_ptr->AddRef(); }
        }

        ~RefPtr()
        {
            if (m_ptr != nullptr) { m_ptr->Release(); }
        }

        RefPtr& operator=(const RefPtr& other) noexcept
        {
            if (this != &other)
            {
                if (other.m_ptr != nullptr) { other.m_ptr->AddRef(); }
                if (m_ptr != nullptr) { m_ptr->Release(); }
                m_ptr = other.m_ptr;
            }
            return *this;
        }

        RefPtr& operator=(RefPtr&& other) noexcept
        {
            if (this != &other)
            {
                if (m_ptr != nullptr) { m_ptr->Release(); }
                m_ptr = other.m_ptr;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        void Reset() noexcept
        {
            if (m_ptr != nullptr) { m_ptr->Release(); }
            m_ptr = nullptr;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator*() const noexcept { return *m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

        [[nodiscard]] friend bool operator==(const RefPtr& a, const RefPtr& b) noexcept
        {
            return a.m_ptr == b.m_ptr;
        }

    private:
        T* m_ptr = nullptr;
    };

    template <typename T, typename... Args>
    RefPtr<T> MakeRef(IAllocator& allocator, Args&&... args)
    {
        static_assert(std::is_base_of_v<RefCounted, T>, "MakeRef requires a RefCounted-derived type.");

        void* memory = allocator.Allocate(sizeof(T), alignof(T));
        if (memory == nullptr)
        {
            return RefPtr<T>{};
        }

        T* object = ::new (memory) T(Forward<Args>(args)...);
        object->SetOwningAllocator(&allocator);
        return RefPtr<T>{ object }; // AddRef -> strong count becomes 1
    }

    // =======================================================================
    // UniquePtr — sole ownership; frees through the owning allocator.
    // =======================================================================
    template <typename T>
    class UniquePtr
    {
    public:
        UniquePtr() noexcept = default;
        UniquePtr(decltype(nullptr)) noexcept {}

        UniquePtr(T* pointer, IAllocator& allocator) noexcept
            : m_ptr(pointer), m_allocator(&allocator) {}

        UniquePtr(UniquePtr&& other) noexcept
            : m_ptr(other.m_ptr), m_allocator(other.m_allocator)
        {
            other.m_ptr = nullptr;
        }

        UniquePtr& operator=(UniquePtr&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_ptr = other.m_ptr;
                m_allocator = other.m_allocator;
                other.m_ptr = nullptr;
            }
            return *this;
        }

        UniquePtr(const UniquePtr&) = delete;
        UniquePtr& operator=(const UniquePtr&) = delete;

        ~UniquePtr() { Reset(); }

        void Reset() noexcept
        {
            if (m_ptr != nullptr && m_allocator != nullptr)
            {
                m_allocator->Delete(m_ptr);
            }
            m_ptr = nullptr;
        }

        [[nodiscard]] T* Release() noexcept
        {
            T* released = m_ptr;
            m_ptr = nullptr;
            return released;
        }

        [[nodiscard]] T* Get() const noexcept { return m_ptr; }
        [[nodiscard]] T* operator->() const noexcept { return m_ptr; }
        [[nodiscard]] T& operator*() const noexcept { return *m_ptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }

    private:
        T* m_ptr = nullptr;
        IAllocator* m_allocator = nullptr;
    };

    template <typename T, typename... Args>
    [[nodiscard]] UniquePtr<T> MakeUnique(IAllocator& allocator, Args&&... args)
    {
        T* object = allocator.New<T>(Forward<Args>(args)...);
        return UniquePtr<T>{ object, allocator };
    }
}
