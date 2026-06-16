module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:span;

import :base;

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
}
