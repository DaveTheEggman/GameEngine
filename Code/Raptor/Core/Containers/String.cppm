// Raptor Core — :string partition
//
// String types. `String` is the primary wide string (UTF-16 / char16_t);
// `UTF8String` is the secondary UTF-8 type. Both are aliases of one
// allocator-backed BasicString<CharT>, each with a matching view.
//
// NOTE: cross-encoding transcoding (UTF-16 <-> UTF-8) and small-string
// optimization are deferred; see Documentation/Planning/Core.md §4.5.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"

export module raptor.core:string;

import :base;
import :memory;

export namespace raptor::core
{
    template <typename CharT>
    [[nodiscard]] constexpr usize CStringLength(const CharT* str) noexcept
    {
        if (str == nullptr)
        {
            return 0;
        }
        usize length = 0;
        while (str[length] != CharT(0))
        {
            ++length;
        }
        return length;
    }

    // =======================================================================
    // BasicStringView — non-owning view over a contiguous character range.
    // =======================================================================
    template <typename CharT>
    class BasicStringView
    {
    public:
        using ValueType = CharT;

        constexpr BasicStringView() noexcept = default;
        constexpr BasicStringView(const CharT* data, usize size) noexcept : m_data(data), m_size(size) {}
        constexpr BasicStringView(const CharT* str) noexcept : m_data(str), m_size(CStringLength(str)) {}

        [[nodiscard]] constexpr const CharT* Data() const noexcept { return m_data; }
        [[nodiscard]] constexpr usize Size() const noexcept { return m_size; }
        [[nodiscard]] constexpr usize Length() const noexcept { return m_size; }
        [[nodiscard]] constexpr bool IsEmpty() const noexcept { return m_size == 0; }

        [[nodiscard]] constexpr CharT operator[](usize index) const noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return m_data[index];
        }

        [[nodiscard]] constexpr const CharT* begin() const noexcept { return m_data; }
        [[nodiscard]] constexpr const CharT* end() const noexcept { return m_data + m_size; }

        [[nodiscard]] constexpr BasicStringView SubStr(usize offset, usize count) const noexcept
        {
            RAPTOR_ASSERT(offset + count <= m_size);
            return BasicStringView{ m_data + offset, count };
        }

        [[nodiscard]] constexpr bool StartsWith(BasicStringView prefix) const noexcept
        {
            return prefix.m_size <= m_size && BasicStringView{ m_data, prefix.m_size } == prefix;
        }

        [[nodiscard]] constexpr bool EndsWith(BasicStringView suffix) const noexcept
        {
            return suffix.m_size <= m_size
                && BasicStringView{ m_data + (m_size - suffix.m_size), suffix.m_size } == suffix;
        }

        [[nodiscard]] friend constexpr bool operator==(BasicStringView a, BasicStringView b) noexcept
        {
            if (a.m_size != b.m_size)
            {
                return false;
            }
            for (usize i = 0; i < a.m_size; ++i)
            {
                if (a.m_data[i] != b.m_data[i])
                {
                    return false;
                }
            }
            return true;
        }

    private:
        const CharT* m_data = nullptr;
        usize m_size = 0;
    };

    // =======================================================================
    // BasicString — allocator-backed, null-terminated, growable string.
    // =======================================================================
    template <typename CharT>
    class BasicString
    {
    public:
        using ValueType = CharT;
        using View = BasicStringView<CharT>;

        BasicString() noexcept : m_allocator(&DefaultAllocator()) {}
        explicit BasicString(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        BasicString(const CharT* str, IAllocator& allocator = DefaultAllocator())
            : m_allocator(&allocator)
        {
            Append(str, CStringLength(str));
        }

        BasicString(View view, IAllocator& allocator = DefaultAllocator())
            : m_allocator(&allocator)
        {
            Append(view.Data(), view.Size());
        }

        BasicString(const BasicString& other) : m_allocator(other.m_allocator)
        {
            Append(other.m_data, other.m_size);
        }

        BasicString(BasicString&& other) noexcept
            : m_data(other.m_data), m_size(other.m_size),
              m_capacity(other.m_capacity), m_allocator(other.m_allocator)
        {
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        BasicString& operator=(const BasicString& other)
        {
            if (this != &other)
            {
                Clear();
                Append(other.m_data, other.m_size);
            }
            return *this;
        }

        BasicString& operator=(BasicString&& other) noexcept
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

        ~BasicString() { Destroy(); }

        // --- capacity ------------------------------------------------------
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] usize Length() const noexcept { return m_size; }
        [[nodiscard]] usize Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }

        void Reserve(usize newCapacity)
        {
            if (newCapacity <= m_capacity)
            {
                return;
            }

            // +1 for the null terminator.
            CharT* newData = static_cast<CharT*>(
                m_allocator->Allocate((newCapacity + 1) * sizeof(CharT), alignof(CharT)));
            RAPTOR_ASSERT_MSG(newData != nullptr, "String allocation failed");

            if (m_data != nullptr)
            {
                MemCopy(newData, m_data, (m_size + 1) * sizeof(CharT));
                m_allocator->Free(m_data);
            }
            else
            {
                newData[0] = CharT(0);
            }

            m_data = newData;
            m_capacity = newCapacity;
        }

        void Clear() noexcept
        {
            m_size = 0;
            if (m_data != nullptr)
            {
                m_data[0] = CharT(0);
            }
        }

        // --- append --------------------------------------------------------
        void Append(const CharT* str, usize count)
        {
            if (count == 0)
            {
                return;
            }
            EnsureCapacity(m_size + count);
            MemCopy(m_data + m_size, str, count * sizeof(CharT));
            m_size += count;
            m_data[m_size] = CharT(0);
        }

        void Append(View view) { Append(view.Data(), view.Size()); }

        void PushBack(CharT ch)
        {
            EnsureCapacity(m_size + 1);
            m_data[m_size++] = ch;
            m_data[m_size] = CharT(0);
        }

        BasicString& operator+=(View view) { Append(view); return *this; }
        BasicString& operator+=(const CharT* str) { Append(str, CStringLength(str)); return *this; }
        BasicString& operator+=(CharT ch) { PushBack(ch); return *this; }

        // --- access --------------------------------------------------------
        [[nodiscard]] CharT& operator[](usize index) noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return m_data[index];
        }
        [[nodiscard]] const CharT& operator[](usize index) const noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return m_data[index];
        }

        // Always null-terminated.
        [[nodiscard]] const CharT* CStr() const noexcept { return m_data != nullptr ? m_data : &s_empty; }
        [[nodiscard]] CharT* Data() noexcept { return m_data; }
        [[nodiscard]] const CharT* Data() const noexcept { return CStr(); }

        [[nodiscard]] View AsView() const noexcept { return View{ CStr(), m_size }; }
        operator View() const noexcept { return AsView(); }

        [[nodiscard]] CharT* begin() noexcept { return m_data; }
        [[nodiscard]] CharT* end() noexcept { return m_data + m_size; }
        [[nodiscard]] const CharT* begin() const noexcept { return CStr(); }
        [[nodiscard]] const CharT* end() const noexcept { return CStr() + m_size; }

    private:
        void EnsureCapacity(usize required)
        {
            if (required > m_capacity)
            {
                const usize doubled = m_capacity * 2;
                const usize next = (required > doubled) ? required : doubled;
                Reserve(next < kInitialCapacity ? kInitialCapacity : next);
            }
        }

        void Destroy() noexcept
        {
            if (m_data != nullptr)
            {
                m_allocator->Free(m_data);
                m_data = nullptr;
            }
            m_size = 0;
            m_capacity = 0;
        }

        static constexpr usize kInitialCapacity = 16;
        static constexpr CharT s_empty = CharT(0);

        CharT* m_data = nullptr;
        usize m_size = 0;
        usize m_capacity = 0;
        IAllocator* m_allocator = nullptr;
    };

    template <typename CharT>
    constexpr CharT BasicString<CharT>::s_empty;

    // Comparison as free function templates (not hidden friends): friends
    // defined in an exported module class can get strong per-TU symbols under
    // GCC, colliding at link; template free functions have COMDAT linkage.
    template <typename CharT>
    [[nodiscard]] bool operator==(const BasicString<CharT>& a, const BasicString<CharT>& b) noexcept
    {
        return a.AsView() == b.AsView();
    }
    template <typename CharT>
    [[nodiscard]] bool operator==(const BasicString<CharT>& a, BasicStringView<CharT> b) noexcept
    {
        return a.AsView() == b;
    }
    template <typename CharT>
    [[nodiscard]] bool operator==(const BasicString<CharT>& a, const CharT* b) noexcept
    {
        return a.AsView() == BasicStringView<CharT>{ b };
    }

    // =======================================================================
    // Aliases — String is wide (UTF-16); UTF8String is the UTF-8 secondary.
    // =======================================================================
    using StringView = BasicStringView<widechar>;
    using String = BasicString<widechar>;

    using UTF8StringView = BasicStringView<utf8char>;
    using UTF8String = BasicString<utf8char>;
}
