module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module foundation.core:fixed_array;

import :base;
import :span;

export namespace foundation::core
{
    // =======================================================================
    // FixedArray - fixed-capacity, stack-allocated array with a live count.
    // For small collections with a known upper bound (e.g. render-pass color
    // attachments). No heap allocation; copyable as a value.
    // =======================================================================
    template <typename T, usize Capacity>
    struct FixedArray
    {
        T items[Capacity]{};
        usize count = 0;

        void Add(const T& item)
        {
            DIAGNOSTIC_ASSERT(count < Capacity);
            items[count++] = item;
        }

        void Clear() noexcept { count = 0; }

        [[nodiscard]] usize Size() const noexcept { return count; }
        [[nodiscard]] bool IsEmpty() const noexcept { return count == 0; }
        [[nodiscard]] static constexpr usize CapacityValue() noexcept { return Capacity; }

        [[nodiscard]] T& operator[](usize i) noexcept { return items[i]; }
        [[nodiscard]] const T& operator[](usize i) const noexcept { return items[i]; }

        [[nodiscard]] T* begin() noexcept { return items; }
        [[nodiscard]] const T* begin() const noexcept { return items; }
        [[nodiscard]] T* end() noexcept { return items + count; }
        [[nodiscard]] const T* end() const noexcept { return items + count; }

        [[nodiscard]] Span<const T> View() const noexcept { return Span<const T>(items, count); }
    };
}
