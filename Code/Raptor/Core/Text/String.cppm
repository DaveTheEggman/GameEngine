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
#include <charconv>

export module raptor.core:string;

import :base;
import :allocator;
import :hash;

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

    private:
        const CharT* m_data = nullptr;
        usize m_size = 0;
    };

    // =======================================================================
    // BasicString — null-terminated, growable string with small-string
    // optimization: short strings live inline; longer ones move to the heap.
    // =======================================================================
    template <typename CharT>
    class BasicString
    {
    public:
        using ValueType = CharT;
        using View = BasicStringView<CharT>;

        BasicString() noexcept : m_allocator(&DefaultAllocator()) { m_storage.inlineBuf[0] = CharT(0); }
        explicit BasicString(IAllocator& allocator) noexcept : m_allocator(&allocator) { m_storage.inlineBuf[0] = CharT(0); }

        BasicString(const CharT* str, IAllocator& allocator = DefaultAllocator())
            : m_allocator(&allocator)
        {
            m_storage.inlineBuf[0] = CharT(0);
            Append(str, CStringLength(str));
        }

        BasicString(View view, IAllocator& allocator = DefaultAllocator())
            : m_allocator(&allocator)
        {
            m_storage.inlineBuf[0] = CharT(0);
            Append(view.Data(), view.Size());
        }

        BasicString(const BasicString& other) : m_allocator(other.m_allocator)
        {
            m_storage.inlineBuf[0] = CharT(0);
            Append(other.Data(), other.m_size);
        }

        BasicString(BasicString&& other) noexcept : m_allocator(other.m_allocator)
        {
            AdoptOrCopy(other);
        }

        BasicString& operator=(const BasicString& other)
        {
            if (this != &other)
            {
                Clear();
                Append(other.Data(), other.m_size);
            }
            return *this;
        }

        BasicString& operator=(BasicString&& other) noexcept
        {
            if (this != &other)
            {
                FreeHeap();
                m_allocator = other.m_allocator;
                AdoptOrCopy(other);
            }
            return *this;
        }

        ~BasicString() { FreeHeap(); }

        // --- capacity ------------------------------------------------------
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] usize Length() const noexcept { return m_size; }
        [[nodiscard]] usize Capacity() const noexcept { return m_isHeap ? m_storage.heap.capacity : kInlineCapacity; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }
        [[nodiscard]] bool IsSmall() const noexcept { return !m_isHeap; }

        void Reserve(usize newCapacity)
        {
            if (newCapacity <= Capacity())
            {
                return;
            }

            // +1 for the null terminator.
            CharT* newData = static_cast<CharT*>(
                m_allocator->Allocate((newCapacity + 1) * sizeof(CharT), alignof(CharT)));
            RAPTOR_ASSERT_MSG(newData != nullptr, "String allocation failed");

            MemCopy(newData, Data(), (m_size + 1) * sizeof(CharT)); // copy incl. terminator
            FreeHeap();
            m_storage.heap.data = newData;
            m_storage.heap.capacity = newCapacity;
            m_isHeap = true;
        }

        void Clear() noexcept
        {
            m_size = 0;
            Data()[0] = CharT(0);
        }

        // --- append --------------------------------------------------------
        void Append(const CharT* str, usize count)
        {
            if (count == 0)
            {
                return;
            }
            EnsureCapacity(m_size + count);
            CharT* data = Data();
            MemCopy(data + m_size, str, count * sizeof(CharT));
            m_size += count;
            data[m_size] = CharT(0);
        }

        void Append(View view) { Append(view.Data(), view.Size()); }

        void PushBack(CharT ch)
        {
            EnsureCapacity(m_size + 1);
            CharT* data = Data();
            data[m_size++] = ch;
            data[m_size] = CharT(0);
        }

        // Single-character append (alias for PushBack) — lets String serve as a
        // format sink alongside its Append(view)/Append(ptr,len) overloads.
        void Append(CharT ch) { PushBack(ch); }

        BasicString& operator+=(View view) { Append(view); return *this; }
        BasicString& operator+=(const CharT* str) { Append(str, CStringLength(str)); return *this; }
        BasicString& operator+=(CharT ch) { PushBack(ch); return *this; }

        // --- access --------------------------------------------------------
        [[nodiscard]] CharT& operator[](usize index) noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return Data()[index];
        }
        [[nodiscard]] const CharT& operator[](usize index) const noexcept
        {
            RAPTOR_ASSERT(index < m_size);
            return Data()[index];
        }

        // Always null-terminated.
        [[nodiscard]] CharT* Data() noexcept { return m_isHeap ? m_storage.heap.data : m_storage.inlineBuf; }
        [[nodiscard]] const CharT* Data() const noexcept { return m_isHeap ? m_storage.heap.data : m_storage.inlineBuf; }
        [[nodiscard]] const CharT* CStr() const noexcept { return Data(); }

        [[nodiscard]] View AsView() const noexcept { return View{ Data(), m_size }; }
        operator View() const noexcept { return AsView(); }

        [[nodiscard]] CharT* begin() noexcept { return Data(); }
        [[nodiscard]] CharT* end() noexcept { return Data() + m_size; }
        [[nodiscard]] const CharT* begin() const noexcept { return Data(); }
        [[nodiscard]] const CharT* end() const noexcept { return Data() + m_size; }

    private:
        static constexpr usize kInlineBytes = 3 * sizeof(void*);
        static constexpr usize kInlineCapacity = (kInlineBytes / sizeof(CharT)) > 1
                                                      ? (kInlineBytes / sizeof(CharT)) - 1 : 1;
        static constexpr usize kInitialHeapCapacity = (kInlineCapacity + 1) * 2;

        void EnsureCapacity(usize required)
        {
            const usize capacity = Capacity();
            if (required > capacity)
            {
                const usize doubled = capacity * 2;
                const usize next = (required > doubled) ? required : doubled;
                Reserve(next < kInitialHeapCapacity ? kInitialHeapCapacity : next);
            }
        }

        void FreeHeap() noexcept
        {
            if (m_isHeap && m_storage.heap.data != nullptr)
            {
                m_allocator->Free(m_storage.heap.data);
            }
            m_isHeap = false;
        }

        // Takes `other`'s buffer (heap) or copies its inline data; leaves
        // `other` empty. Assumes *this owns no heap buffer.
        void AdoptOrCopy(BasicString& other) noexcept
        {
            if (other.m_isHeap)
            {
                m_isHeap = true;
                m_storage.heap = other.m_storage.heap;
            }
            else
            {
                m_isHeap = false;
                MemCopy(m_storage.inlineBuf, other.m_storage.inlineBuf, (other.m_size + 1) * sizeof(CharT));
            }
            m_size = other.m_size;
            other.m_isHeap = false;
            other.m_size = 0;
            other.m_storage.inlineBuf[0] = CharT(0);
        }

        union Storage
        {
            struct
            {
                CharT* data;
                usize capacity;
            } heap;
            CharT inlineBuf[kInlineCapacity + 1];
        };

        Storage m_storage;
        usize m_size = 0;
        IAllocator* m_allocator = nullptr;
        bool m_isHeap = false;
    };

    // Comparison as free function templates (not hidden friends): friends
    // defined in an exported module class can get strong per-TU symbols under
    // GCC, colliding at link; template free functions have COMDAT linkage.
    template <typename CharT>
    [[nodiscard]] constexpr bool operator==(BasicStringView<CharT> a, BasicStringView<CharT> b) noexcept
    {
        if (a.Size() != b.Size()) { return false; }
        for (usize i = 0; i < a.Size(); ++i)
        {
            if (a.Data()[i] != b.Data()[i]) { return false; }
        }
        return true;
    }
    // A C-string literal can't deduce CharT for the view==view template, so a
    // dedicated overload covers `view == "lit"` (and, via the C++20 reversed
    // candidate, `"lit" == view`).
    template <typename CharT>
    [[nodiscard]] constexpr bool operator==(BasicStringView<CharT> a, const CharT* b) noexcept
    {
        return a == BasicStringView<CharT>{ b };
    }
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

    // =======================================================================
    // StringBuilder — incrementally builds a wide String, including numbers
    // (formatted as ASCII via <charconv> and widened).
    // =======================================================================
    class StringBuilder
    {
    public:
        StringBuilder() = default;
        explicit StringBuilder(IAllocator& allocator) : m_string(allocator) {}

        StringBuilder& Append(StringView view) { m_string.Append(view); return *this; }
        StringBuilder& Append(const widechar* str) { m_string.Append(StringView{ str }); return *this; }
        StringBuilder& Append(widechar ch) { m_string.PushBack(ch); return *this; }

        // Appends an ASCII C-string, widening each byte.
        StringBuilder& AppendAscii(const char* str)
        {
            for (usize i = 0; str[i] != '\0'; ++i)
            {
                m_string.PushBack(static_cast<widechar>(static_cast<unsigned char>(str[i])));
            }
            return *this;
        }

        StringBuilder& AppendInt(i64 value) { return AppendChars(value); }
        StringBuilder& AppendUInt(u64 value) { return AppendChars(value); }
        StringBuilder& AppendFloat(f64 value) { return AppendChars(value); }
        StringBuilder& AppendBool(bool value) { return AppendAscii(value ? "true" : "false"); }

        void Clear() noexcept { m_string.Clear(); }
        [[nodiscard]] usize Size() const noexcept { return m_string.Size(); }
        [[nodiscard]] StringView View() const noexcept { return m_string.AsView(); }

        // Copy out, or move the built string out (leaving the builder empty).
        [[nodiscard]] const String& Str() const noexcept { return m_string; }
        [[nodiscard]] String Take() noexcept { return Move(m_string); }

    private:
        template <typename T>
        StringBuilder& AppendChars(T value)
        {
            char temp[48];
            const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), value);
            for (char* p = temp; p != result.ptr; ++p)
            {
                m_string.PushBack(static_cast<widechar>(static_cast<unsigned char>(*p)));
            }
            return *this;
        }

        String m_string;
    };

    // =======================================================================
    // UTF-8 <-> UTF-16 transcoding. Invalid sequences become U+FFFD.
    // =======================================================================
    [[nodiscard]] inline String ToWide(UTF8StringView utf8, IAllocator& allocator = DefaultAllocator())
    {
        String result(allocator);
        const usize size = utf8.Size();
        usize i = 0;
        while (i < size)
        {
            const u8 lead = static_cast<u8>(utf8[i]);
            u32 codepoint;
            usize extra;
            if (lead < 0x80u) { codepoint = lead; extra = 0; }
            else if ((lead & 0xE0u) == 0xC0u) { codepoint = lead & 0x1Fu; extra = 1; }
            else if ((lead & 0xF0u) == 0xE0u) { codepoint = lead & 0x0Fu; extra = 2; }
            else if ((lead & 0xF8u) == 0xF0u) { codepoint = lead & 0x07u; extra = 3; }
            else { codepoint = 0xFFFDu; extra = 0; }
            ++i;

            bool valid = true;
            for (usize k = 0; k < extra; ++k)
            {
                if (i >= size || (static_cast<u8>(utf8[i]) & 0xC0u) != 0x80u) { valid = false; break; }
                codepoint = (codepoint << 6) | (static_cast<u8>(utf8[i]) & 0x3Fu);
                ++i;
            }
            if (!valid) { codepoint = 0xFFFDu; }

            if (codepoint <= 0xFFFFu)
            {
                result.PushBack(static_cast<widechar>(codepoint));
            }
            else
            {
                codepoint -= 0x10000u;
                result.PushBack(static_cast<widechar>(0xD800u + (codepoint >> 10)));
                result.PushBack(static_cast<widechar>(0xDC00u + (codepoint & 0x3FFu)));
            }
        }
        return result;
    }

    [[nodiscard]] inline UTF8String ToUTF8(StringView wide, IAllocator& allocator = DefaultAllocator())
    {
        UTF8String result(allocator);
        const usize size = wide.Size();
        usize i = 0;
        while (i < size)
        {
            u32 codepoint = static_cast<u16>(wide[i]);
            ++i;
            if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) // high surrogate
            {
                if (i < size)
                {
                    const u16 low = static_cast<u16>(wide[i]);
                    if (low >= 0xDC00u && low <= 0xDFFFu)
                    {
                        codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                        ++i;
                    }
                    else { codepoint = 0xFFFDu; }
                }
                else { codepoint = 0xFFFDu; }
            }
            else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) { codepoint = 0xFFFDu; } // lone low

            if (codepoint < 0x80u)
            {
                result.PushBack(static_cast<utf8char>(codepoint));
            }
            else if (codepoint < 0x800u)
            {
                result.PushBack(static_cast<utf8char>(0xC0u | (codepoint >> 6)));
                result.PushBack(static_cast<utf8char>(0x80u | (codepoint & 0x3Fu)));
            }
            else if (codepoint < 0x10000u)
            {
                result.PushBack(static_cast<utf8char>(0xE0u | (codepoint >> 12)));
                result.PushBack(static_cast<utf8char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
                result.PushBack(static_cast<utf8char>(0x80u | (codepoint & 0x3Fu)));
            }
            else
            {
                result.PushBack(static_cast<utf8char>(0xF0u | (codepoint >> 18)));
                result.PushBack(static_cast<utf8char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
                result.PushBack(static_cast<utf8char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
                result.PushBack(static_cast<utf8char>(0x80u | (codepoint & 0x3Fu)));
            }
        }
        return result;
    }
    // Hash specializations so the string types work as hashed-container keys.
    // (The Hash<T> primary template + HashBytes live in :hash.)
    template <typename CharT>
    struct Hash<BasicStringView<CharT>>
    {
        [[nodiscard]] u64 operator()(BasicStringView<CharT> view) const noexcept
        {
            return HashBytes(view.Data(), view.Size() * sizeof(CharT));
        }
    };

    template <typename CharT>
    struct Hash<BasicString<CharT>>
    {
        [[nodiscard]] u64 operator()(const BasicString<CharT>& str) const noexcept
        {
            return HashBytes(str.Data(), str.Size() * sizeof(CharT));
        }
    };
}
