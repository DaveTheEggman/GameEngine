// Raptor Core — :unique_ptr partition
//
// UniquePtr<T>: sole ownership; frees through the owning allocator. Allocation
// is explicit (the allocator is passed at creation), matching engine policy.

module;
#include "Core/Prelude.h"

export module raptor.core:unique_ptr;

import :base;
import :memory;

export namespace raptor::core
{
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
