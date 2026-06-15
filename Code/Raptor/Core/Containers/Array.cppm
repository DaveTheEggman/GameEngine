// Raptor Core — :containers partition (Span + Array)
//
// Allocator-aware containers. Span is a non-owning view; Array is a growable,
// allocator-backed dynamic array. More containers (String, HashMap, ...) land
// as additional partitions.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <new>          // placement new
#include <type_traits>

export module raptor.core:containers;

import :base;
import :memory;

export namespace raptor::core
{
    // =======================================================================
    // Span — non-owning view over contiguous elements.
    // =======================================================================
    template <typename T>
    class Span
    {
    public:
        Span() noexcept = default;
        Span(T* data, usize size) noexcept : m_data(data), m_size(size) {}

        template <usize N>
        Span(T (&array)[N]) noexcept : m_data(array), m_size(N) {}

        [[nodiscard]] T* Data() const noexcept { return m_data; }
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }

        [[nodiscard]] T& operator[](usize index) const noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return m_data[index];
        }

        [[nodiscard]] T& Front() const noexcept { RAPTOR_ASSERT(m_size > 0); return m_data[0]; }
        [[nodiscard]] T& Back() const noexcept { RAPTOR_ASSERT(m_size > 0); return m_data[m_size - 1]; }

        [[nodiscard]] Span SubSpan(usize offset, usize count) const noexcept
        {
            RAPTOR_ASSERT(offset + count <= m_size);
            return Span{ m_data + offset, count };
        }

        [[nodiscard]] T* begin() const noexcept { return m_data; }
        [[nodiscard]] T* end() const noexcept { return m_data + m_size; }

    private:
        T* m_data = nullptr;
        usize m_size = 0;
    };

    // =======================================================================
    // Array — growable, allocator-backed dynamic array.
    // =======================================================================
    template <typename T>
    class Array
    {
    public:
        Array() noexcept : m_allocator(&DefaultAllocator()) {}
        explicit Array(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        Array(const Array& other) : m_allocator(other.m_allocator)
        {
            Reserve(other.m_size);
            for (usize i = 0; i < other.m_size; ++i)
            {
                ::new (&m_data[i]) T(other.m_data[i]);
            }
            m_size = other.m_size;
        }

        Array(Array&& other) noexcept
            : m_data(other.m_data), m_size(other.m_size),
              m_capacity(other.m_capacity), m_allocator(other.m_allocator)
        {
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        Array& operator=(const Array& other)
        {
            if (this != &other)
            {
                Clear();
                Reserve(other.m_size);
                for (usize i = 0; i < other.m_size; ++i)
                {
                    ::new (&m_data[i]) T(other.m_data[i]);
                }
                m_size = other.m_size;
            }
            return *this;
        }

        Array& operator=(Array&& other) noexcept
        {
            if (this != &other)
            {
                Destroy();
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                m_allocator = other.m_allocator;
                other.m_data = nullptr;
                other.m_size = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        ~Array() { Destroy(); }

        // --- capacity ------------------------------------------------------
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] usize Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }

        void Reserve(usize newCapacity)
        {
            if (newCapacity <= m_capacity)
            {
                return;
            }

            T* newData = static_cast<T*>(m_allocator->Allocate(newCapacity * sizeof(T), alignof(T)));
            RAPTOR_ASSERT_MSG(newData != nullptr, "Array allocation failed");

            for (usize i = 0; i < m_size; ++i)
            {
                ::new (&newData[i]) T(Move(m_data[i]));
                m_data[i].~T();
            }

            if (m_data != nullptr)
            {
                m_allocator->Free(m_data);
            }
            m_data = newData;
            m_capacity = newCapacity;
        }

        void Resize(usize newSize)
        {
            if (newSize < m_size)
            {
                for (usize i = newSize; i < m_size; ++i)
                {
                    m_data[i].~T();
                }
            }
            else if (newSize > m_size)
            {
                Reserve(newSize);
                for (usize i = m_size; i < newSize; ++i)
                {
                    ::new (&m_data[i]) T();
                }
            }
            m_size = newSize;
        }

        // Destroys all elements; keeps the allocated capacity.
        void Clear() noexcept
        {
            for (usize i = 0; i < m_size; ++i)
            {
                m_data[i].~T();
            }
            m_size = 0;
        }

        // --- element access ------------------------------------------------
        [[nodiscard]] T& operator[](usize index) noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return m_data[index];
        }
        [[nodiscard]] const T& operator[](usize index) const noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return m_data[index];
        }

        [[nodiscard]] T& Front() noexcept { RAPTOR_ASSERT(m_size > 0); return m_data[0]; }
        [[nodiscard]] T& Back() noexcept { RAPTOR_ASSERT(m_size > 0); return m_data[m_size - 1]; }

        [[nodiscard]] T* Data() noexcept { return m_data; }
        [[nodiscard]] const T* Data() const noexcept { return m_data; }

        // --- modifiers -----------------------------------------------------
        T& PushBack(const T& value)
        {
            EnsureCapacityForOne();
            ::new (&m_data[m_size]) T(value);
            return m_data[m_size++];
        }

        T& PushBack(T&& value)
        {
            EnsureCapacityForOne();
            ::new (&m_data[m_size]) T(Move(value));
            return m_data[m_size++];
        }

        template <typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            EnsureCapacityForOne();
            ::new (&m_data[m_size]) T(Forward<Args>(args)...);
            return m_data[m_size++];
        }

        void PopBack() noexcept
        {
            RAPTOR_ASSERT(m_size > 0);
            m_data[--m_size].~T();
        }

        // Removes element `index`, shifting the tail down (order preserved).
        void RemoveAt(usize index)
        {
            RAPTOR_ASSERT(index < m_size);
            for (usize i = index; i + 1 < m_size; ++i)
            {
                m_data[i] = Move(m_data[i + 1]);
            }
            m_data[--m_size].~T();
        }

        // Removes element `index` by swapping in the last element (O(1), order not preserved).
        void RemoveAtSwap(usize index)
        {
            RAPTOR_ASSERT(index < m_size);
            if (index != m_size - 1)
            {
                m_data[index] = Move(m_data[m_size - 1]);
            }
            m_data[--m_size].~T();
        }

        // --- views / iteration ---------------------------------------------
        [[nodiscard]] Span<T> AsSpan() noexcept { return Span<T>{ m_data, m_size }; }
        [[nodiscard]] Span<const T> AsSpan() const noexcept { return Span<const T>{ m_data, m_size }; }

        [[nodiscard]] T* begin() noexcept { return m_data; }
        [[nodiscard]] T* end() noexcept { return m_data + m_size; }
        [[nodiscard]] const T* begin() const noexcept { return m_data; }
        [[nodiscard]] const T* end() const noexcept { return m_data + m_size; }

    private:
        void EnsureCapacityForOne()
        {
            if (m_size == m_capacity)
            {
                Reserve(m_capacity == 0 ? kInitialCapacity : m_capacity * 2);
            }
        }

        void Destroy() noexcept
        {
            Clear();
            if (m_data != nullptr)
            {
                m_allocator->Free(m_data);
                m_data = nullptr;
            }
            m_capacity = 0;
        }

        static constexpr usize kInitialCapacity = 8;

        T* m_data = nullptr;
        usize m_size = 0;
        usize m_capacity = 0;
        IAllocator* m_allocator = nullptr;
    };
}
