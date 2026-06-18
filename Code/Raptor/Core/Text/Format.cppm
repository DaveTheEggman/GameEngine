// Raptor Core — :format partition
//
// Lightweight typesafe text formatting (no iostreams, no exceptions). Output
// targets a growable, allocator-backed wide (UTF-16) buffer; `{}` marks a
// substitution, `{{`/`}}` are literal braces. Numbers go through <charconv>
// (std::to_chars, allocation-free, locale-independent) and are widened into the
// buffer digit by digit. Wide is the API surface; sinks transcode to UTF-8
// bytes at the OS edge.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <charconv>
#include <type_traits>

export module raptor.core:format;

import :base;
import :allocator;
import :string;
import :guid;

export namespace raptor::core
{
    class FormatBuffer
    {
    public:
        FormatBuffer() noexcept : m_allocator(&DefaultAllocator()) {}
        explicit FormatBuffer(IAllocator& allocator) noexcept : m_allocator(&allocator) {}

        FormatBuffer(const FormatBuffer&) = delete;
        FormatBuffer& operator=(const FormatBuffer&) = delete;

        ~FormatBuffer()
        {
            if (m_data != nullptr) { m_allocator->Free(m_data); }
        }

        void Append(widechar c)
        {
            EnsureCapacity(m_size + 1);
            m_data[m_size++] = c;
            m_data[m_size] = u'\0';
        }

        void Append(const widechar* text, usize length)
        {
            if (length == 0) { return; }
            EnsureCapacity(m_size + length);
            MemCopy(m_data + m_size, text, length * sizeof(widechar));
            m_size += length;
            m_data[m_size] = u'\0';
        }

        void Append(const widechar* cstr)
        {
            usize length = 0;
            while (cstr[length] != u'\0') { ++length; }
            Append(cstr, length);
        }

        void Clear() noexcept
        {
            m_size = 0;
            if (m_data != nullptr) { m_data[0] = u'\0'; }
        }

        [[nodiscard]] const widechar* Data() const noexcept { return m_data != nullptr ? m_data : u""; }
        [[nodiscard]] const widechar* CStr() const noexcept { return Data(); }
        [[nodiscard]] StringView View() const noexcept { return StringView{ Data(), m_size }; }
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }

    private:
        void EnsureCapacity(usize required)
        {
            if (required + 1 <= m_capacity) { return; }

            usize newCapacity = (m_capacity == 0) ? 64 : m_capacity * 2;
            if (newCapacity < required + 1) { newCapacity = required + 1; }

            widechar* newData = static_cast<widechar*>(
                m_allocator->Allocate(newCapacity * sizeof(widechar), alignof(widechar)));
            RAPTOR_ASSERT_MSG(newData != nullptr, "FormatBuffer allocation failed");

            if (m_data != nullptr)
            {
                MemCopy(newData, m_data, (m_size + 1) * sizeof(widechar));
                m_allocator->Free(m_data);
            }
            else
            {
                newData[0] = u'\0';
            }
            m_data = newData;
            m_capacity = newCapacity;
        }

        widechar* m_data = nullptr;
        usize m_size = 0;
        usize m_capacity = 0;
        IAllocator* m_allocator = nullptr;
    };

    // The appenders/format core are generic over the output sink: any type with
    // Append(widechar), Append(const widechar*) and Append(const widechar*, usize)
    // works. FormatBuffer is one such sink; String is another (so formatting can
    // write straight into a String, no scratch buffer).
    namespace detail
    {
        // Widen a run of ASCII bytes (digits/hex from <charconv>) into the sink.
        template <typename Sink>
        void AppendAsciiDigits(Sink& out, const char* text, usize length)
        {
            for (usize i = 0; i < length; ++i)
            {
                out.Append(static_cast<widechar>(static_cast<unsigned char>(text[i])));
            }
        }
    }

    // --- per-type appenders ------------------------------------------------
    template <typename Sink>
    void AppendValue(Sink& out, bool value)
    {
        out.Append(value ? u"true" : u"false");
    }

    template <typename Sink>
    void AppendValue(Sink& out, char value)
    {
        out.Append(static_cast<widechar>(static_cast<unsigned char>(value)));
    }

    template <typename Sink>
    void AppendValue(Sink& out, widechar value) { out.Append(value); }

    template <typename Sink>
    void AppendValue(Sink& out, const widechar* value)
    {
        out.Append(value != nullptr ? value : u"(null)");
    }

    template <typename Sink, typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool>
                  && !std::is_same_v<T, char> && !std::is_same_v<T, widechar>
                  && !std::is_same_v<T, char8_t>)
    void AppendValue(Sink& out, T value)
    {
        char temp[32];
        const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), value);
        detail::AppendAsciiDigits(out, temp, static_cast<usize>(result.ptr - temp));
    }

    template <typename Sink, typename T>
        requires std::is_floating_point_v<T>
    void AppendValue(Sink& out, T value)
    {
        char temp[48];
        const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), value);
        detail::AppendAsciiDigits(out, temp, static_cast<usize>(result.ptr - temp));
    }

    // UTF-8 view: transcode to wide.
    template <typename Sink>
    void AppendValue(Sink& out, UTF8StringView view)
    {
        const String wide = ToWide(view);
        out.Append(wide.Data(), wide.Size());
    }

    // Wide view (and String, via its implicit View conversion): append directly.
    template <typename Sink>
    void AppendValue(Sink& out, StringView view)
    {
        out.Append(view.Data(), view.Size());
    }

    template <typename Sink>
    void AppendValue(Sink& out, Guid value)
    {
        widechar text[37];
        value.ToChars(text);
        out.Append(text, 36);
    }

    template <typename Sink>
    void AppendValue(Sink& out, const void* value)
    {
        out.Append(u"0x");
        char temp[20];
        const auto address = static_cast<u64>(reinterpret_cast<uptr>(value));
        const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), address, 16);
        detail::AppendAsciiDigits(out, temp, static_cast<usize>(result.ptr - temp));
    }

    // --- format core (generic over the output sink) ------------------------
    // --- runtime format (FormatToV): fmt is a wide const widechar* ---------
    template <typename Sink>
    void FormatToV(Sink& out, const widechar* fmt)
    {
        // No remaining args: copy the rest, honouring {{ and }} escapes.
        while (*fmt != u'\0')
        {
            if (fmt[0] == u'{' && fmt[1] == u'{') { out.Append(u'{'); fmt += 2; }
            else if (fmt[0] == u'}' && fmt[1] == u'}') { out.Append(u'}'); fmt += 2; }
            else { out.Append(*fmt++); }
        }
    }

    template <typename Sink, typename T, typename... Rest>
    void FormatToV(Sink& out, const widechar* fmt, const T& value, const Rest&... rest)
    {
        while (*fmt != u'\0')
        {
            if (fmt[0] == u'{' && fmt[1] == u'}')
            {
                AppendValue(out, value);
                FormatToV(out, fmt + 2, rest...);
                return;
            }
            if (fmt[0] == u'{' && fmt[1] == u'{') { out.Append(u'{'); fmt += 2; continue; }
            if (fmt[0] == u'}' && fmt[1] == u'}') { out.Append(u'}'); fmt += 2; continue; }
            out.Append(*fmt++);
        }
        // More args than `{}` placeholders: extra args are ignored.
    }

    // --- compile-time-checked format ---------------------------------------
    namespace detail
    {
        // Calling a non-constexpr function inside a consteval context is a
        // compile error; the name is what shows up in the diagnostic.
        inline void Format_argument_count_does_not_match_the_format_string() {}
    }

    // A format string whose `{}` count is validated at compile time against the
    // argument pack. Constructed implicitly from a wide string literal.
    template <typename... Args>
    struct BasicFormatString
    {
        const widechar* data;

        template <usize N>
        consteval BasicFormatString(const widechar (&str)[N]) : data(str)
        {
            usize placeholders = 0;
            usize i = 0;
            while (i + 1 < N)
            {
                const widechar c0 = str[i];
                const widechar c1 = str[i + 1];
                if (c0 == u'{' && c1 == u'{') { i += 2; }
                else if (c0 == u'}' && c1 == u'}') { i += 2; }
                else if (c0 == u'{' && c1 == u'}') { ++placeholders; i += 2; }
                else { ++i; }
            }
            if (placeholders != sizeof...(Args))
            {
                detail::Format_argument_count_does_not_match_the_format_string();
            }
        }
    };

    template <typename... Args>
    using FormatString = BasicFormatString<std::type_identity_t<Args>...>;

    // Checked entry point: a literal format string's placeholder count must
    // match the argument count (verified by FormatString's consteval ctor).
    template <typename... Args>
    void FormatTo(FormatBuffer& out, FormatString<Args...> fmt, const Args&... args)
    {
        FormatToV(out, fmt.data, args...);
    }

    // Append formatted text straight into a String (no scratch buffer).
    template <typename... Args>
    void AppendFormat(String& out, FormatString<Args...> fmt, const Args&... args)
    {
        FormatToV(out, fmt.data, args...);
    }

    // Build a new String from a format string and arguments.
    template <typename... Args>
    [[nodiscard]] String Format(FormatString<Args...> fmt, const Args&... args)
    {
        String out;
        FormatToV(out, fmt.data, args...);
        return out;
    }
}
