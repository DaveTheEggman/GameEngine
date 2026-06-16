// Raptor Core — :containers partition (Span + Array)
//
// Allocator-aware containers. Span is a non-owning view; Array is a growable,
// allocator-backed dynamic array. More containers (String, HashMap, ...) land
// as additional partitions.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
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
                Construct<T>(&m_data[i], other.m_data[i]);
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
                    Construct<T>(&m_data[i], other.m_data[i]);
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
                Construct<T>(&newData[i], Move(m_data[i]));
                Destruct(&m_data[i]);
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
                    Destruct(&m_data[i]);
                }
            }
            else if (newSize > m_size)
            {
                Reserve(newSize);
                for (usize i = m_size; i < newSize; ++i)
                {
                    Construct<T>(&m_data[i]);
                }
            }
            m_size = newSize;
        }

        // Destroys all elements; keeps the allocated capacity.
        void Clear() noexcept
        {
            for (usize i = 0; i < m_size; ++i)
            {
                Destruct(&m_data[i]);
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
            Construct<T>(&m_data[m_size], value);
            return m_data[m_size++];
        }

        T& PushBack(T&& value)
        {
            EnsureCapacityForOne();
            Construct<T>(&m_data[m_size], Move(value));
            return m_data[m_size++];
        }

        template <typename... Args>
        T& EmplaceBack(Args&&... args)
        {
            EnsureCapacityForOne();
            Construct<T>(&m_data[m_size], Forward<Args>(args)...);
            return m_data[m_size++];
        }

        void PopBack() noexcept
        {
            RAPTOR_ASSERT(m_size > 0);
            Destruct(&m_data[--m_size]);
        }

        // Removes element `index`, shifting the tail down (order preserved).
        void RemoveAt(usize index)
        {
            RAPTOR_ASSERT(index < m_size);
            for (usize i = index; i + 1 < m_size; ++i)
            {
                m_data[i] = Move(m_data[i + 1]);
            }
            Destruct(&m_data[--m_size]);
        }

        // Removes element `index` by swapping in the last element (O(1), order not preserved).
        void RemoveAtSwap(usize index)
        {
            RAPTOR_ASSERT(index < m_size);
            if (index != m_size - 1)
            {
                m_data[index] = Move(m_data[m_size - 1]);
            }
            Destruct(&m_data[--m_size]);
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

    // =======================================================================
    // RingBuffer — fixed-capacity circular FIFO. PushBack/PopFront; full pushes
    // are rejected (returns false). Move-only.
    // =======================================================================
    template <typename T>
    class RingBuffer
    {
    public:
        RingBuffer() noexcept : m_allocator(&DefaultAllocator()) {}

        explicit RingBuffer(usize capacity, IAllocator& allocator = DefaultAllocator())
            : m_allocator(&allocator)
        {
            if (capacity > 0)
            {
                m_data = static_cast<T*>(m_allocator->Allocate(capacity * sizeof(T), alignof(T)));
                RAPTOR_ASSERT_MSG(m_data != nullptr, "RingBuffer allocation failed");
                m_capacity = capacity;
            }
        }

        RingBuffer(RingBuffer&& other) noexcept
            : m_data(other.m_data), m_capacity(other.m_capacity),
              m_head(other.m_head), m_count(other.m_count), m_allocator(other.m_allocator)
        {
            other.m_data = nullptr;
            other.m_capacity = 0;
            other.m_head = 0;
            other.m_count = 0;
        }

        RingBuffer& operator=(RingBuffer&& other) noexcept
        {
            if (this != &other)
            {
                Destroy();
                m_data = other.m_data;
                m_capacity = other.m_capacity;
                m_head = other.m_head;
                m_count = other.m_count;
                m_allocator = other.m_allocator;
                other.m_data = nullptr;
                other.m_capacity = 0;
                other.m_head = 0;
                other.m_count = 0;
            }
            return *this;
        }

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        ~RingBuffer() { Destroy(); }

        [[nodiscard]] usize Size() const noexcept { return m_count; }
        [[nodiscard]] usize Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_count == 0; }
        [[nodiscard]] bool IsFull() const noexcept { return m_count == m_capacity; }

        bool PushBack(const T& value)
        {
            if (IsFull()) { return false; }
            Construct<T>(&m_data[TailIndex()], value);
            ++m_count;
            return true;
        }

        bool PushBack(T&& value)
        {
            if (IsFull()) { return false; }
            Construct<T>(&m_data[TailIndex()], Move(value));
            ++m_count;
            return true;
        }

        // Moves the front element into `out` and removes it; false if empty.
        bool PopFront(T& out)
        {
            if (IsEmpty()) { return false; }
            out = Move(m_data[m_head]);
            Destruct(&m_data[m_head]);
            m_head = (m_head + 1) % m_capacity;
            --m_count;
            return true;
        }

        [[nodiscard]] T& Front() noexcept { RAPTOR_ASSERT(m_count > 0); return m_data[m_head]; }
        [[nodiscard]] T& Back() noexcept
        {
            RAPTOR_ASSERT(m_count > 0);
            return m_data[(m_head + m_count - 1) % m_capacity];
        }

        void Clear() noexcept
        {
            for (usize i = 0; i < m_count; ++i)
            {
                Destruct(&m_data[(m_head + i) % m_capacity]);
            }
            m_head = 0;
            m_count = 0;
        }

    private:
        [[nodiscard]] usize TailIndex() const noexcept { return (m_head + m_count) % m_capacity; }

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

        T* m_data = nullptr;
        usize m_capacity = 0;
        usize m_head = 0;
        usize m_count = 0;
        IAllocator* m_allocator = nullptr;
    };

    // =======================================================================
    // IntrusiveList — doubly-linked list whose link lives in the element.
    // Non-owning: the caller owns the elements. T must derive from
    // IntrusiveListNode. O(1) insert/remove given the element.
    // =======================================================================
    struct IntrusiveListNode
    {
        IntrusiveListNode* prev = nullptr;
        IntrusiveListNode* next = nullptr;
    };

    template <typename T>
    class IntrusiveList
    {
        static_assert(std::is_base_of_v<IntrusiveListNode, T>, "T must derive from IntrusiveListNode.");

    public:
        IntrusiveList() noexcept { Reset(); }

        IntrusiveList(const IntrusiveList&) = delete;
        IntrusiveList& operator=(const IntrusiveList&) = delete;

        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }
        [[nodiscard]] usize Size() const noexcept { return m_size; }

        void PushBack(T& item) noexcept { InsertBefore(&m_sentinel, item); }
        void PushFront(T& item) noexcept { InsertBefore(m_sentinel.next, item); }

        void Remove(T& item) noexcept
        {
            IntrusiveListNode* node = &item;
            node->prev->next = node->next;
            node->next->prev = node->prev;
            node->prev = nullptr;
            node->next = nullptr;
            --m_size;
        }

        [[nodiscard]] T* Front() noexcept
        {
            return IsEmpty() ? nullptr : static_cast<T*>(m_sentinel.next);
        }
        [[nodiscard]] T* Back() noexcept
        {
            return IsEmpty() ? nullptr : static_cast<T*>(m_sentinel.prev);
        }

        // Unlinks all elements (does not destroy them).
        void Clear() noexcept
        {
            IntrusiveListNode* node = m_sentinel.next;
            while (node != &m_sentinel)
            {
                IntrusiveListNode* nextNode = node->next;
                node->prev = nullptr;
                node->next = nullptr;
                node = nextNode;
            }
            Reset();
        }

        class Iterator
        {
        public:
            explicit Iterator(IntrusiveListNode* node) noexcept : m_node(node) {}
            [[nodiscard]] T& operator*() const noexcept { return *static_cast<T*>(m_node); }
            Iterator& operator++() noexcept { m_node = m_node->next; return *this; }
            [[nodiscard]] bool operator!=(const Iterator& other) const noexcept { return m_node != other.m_node; }

        private:
            IntrusiveListNode* m_node;
        };

        [[nodiscard]] Iterator begin() noexcept { return Iterator{ m_sentinel.next }; }
        [[nodiscard]] Iterator end() noexcept { return Iterator{ &m_sentinel }; }

    private:
        void Reset() noexcept
        {
            m_sentinel.next = &m_sentinel;
            m_sentinel.prev = &m_sentinel;
            m_size = 0;
        }

        void InsertBefore(IntrusiveListNode* position, T& item) noexcept
        {
            IntrusiveListNode* node = &item;
            node->prev = position->prev;
            node->next = position;
            position->prev->next = node;
            position->prev = node;
            ++m_size;
        }

        IntrusiveListNode m_sentinel;
        usize m_size = 0;
    };
}
