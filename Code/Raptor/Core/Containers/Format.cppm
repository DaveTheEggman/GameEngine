// Raptor Core — :format partition
//
// Lightweight typesafe text formatting (no iostreams, no exceptions). Numbers
// go through <charconv> (std::to_chars) — allocation-free and locale-independent.
// Output targets a growable, allocator-backed narrow (char) buffer; `{}` marks a
// substitution, `{{`/`}}` are literal braces.
//
// Narrow char (not char8_t) is used deliberately: it is what <charconv> and the
// OS write APIs (System console/file) consume. Wide-string formatting awaits
// UTF-16<->UTF-8 transcoding (deferred).

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include <charconv>
#include <type_traits>

export module raptor.core:format;

import :base;
import :memory;

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

        void Append(char c)
        {
            EnsureCapacity(m_size + 1);
            m_data[m_size++] = c;
            m_data[m_size] = '\0';
        }

        void Append(const char* text, usize length)
        {
            if (length == 0) { return; }
            EnsureCapacity(m_size + length);
            MemCopy(m_data + m_size, text, length);
            m_size += length;
            m_data[m_size] = '\0';
        }

        void Append(const char* cstr)
        {
            usize length = 0;
            while (cstr[length] != '\0') { ++length; }
            Append(cstr, length);
        }

        void Clear() noexcept
        {
            m_size = 0;
            if (m_data != nullptr) { m_data[0] = '\0'; }
        }

        [[nodiscard]] const char* Data() const noexcept { return m_data != nullptr ? m_data : ""; }
        [[nodiscard]] const char* CStr() const noexcept { return Data(); }
        [[nodiscard]] usize Size() const noexcept { return m_size; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_size == 0; }

    private:
        void EnsureCapacity(usize required)
        {
            if (required + 1 <= m_capacity) { return; }

            usize newCapacity = (m_capacity == 0) ? 64 : m_capacity * 2;
            if (newCapacity < required + 1) { newCapacity = required + 1; }

            char* newData = static_cast<char*>(m_allocator->Allocate(newCapacity, alignof(char)));
            RAPTOR_ASSERT_MSG(newData != nullptr, "FormatBuffer allocation failed");

            if (m_data != nullptr)
            {
                MemCopy(newData, m_data, m_size + 1);
                m_allocator->Free(m_data);
            }
            else
            {
                newData[0] = '\0';
            }
            m_data = newData;
            m_capacity = newCapacity;
        }

        char* m_data = nullptr;
        usize m_size = 0;
        usize m_capacity = 0;
        IAllocator* m_allocator = nullptr;
    };

    // --- per-type appenders ------------------------------------------------
    inline void AppendValue(FormatBuffer& out, bool value)
    {
        out.Append(value ? "true" : "false");
    }

    inline void AppendValue(FormatBuffer& out, char value) { out.Append(value); }

    inline void AppendValue(FormatBuffer& out, const char* value)
    {
        out.Append(value != nullptr ? value : "(null)");
    }

    template <typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
    void AppendValue(FormatBuffer& out, T value)
    {
        char temp[32];
        const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), value);
        out.Append(temp, static_cast<usize>(result.ptr - temp));
    }

    template <typename T>
        requires std::is_floating_point_v<T>
    void AppendValue(FormatBuffer& out, T value)
    {
        char temp[48];
        const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), value);
        out.Append(temp, static_cast<usize>(result.ptr - temp));
    }

    inline void AppendValue(FormatBuffer& out, const void* value)
    {
        out.Append("0x");
        char temp[20];
        const auto address = static_cast<u64>(reinterpret_cast<uptr>(value));
        const std::to_chars_result result = std::to_chars(temp, temp + sizeof(temp), address, 16);
        out.Append(temp, static_cast<usize>(result.ptr - temp));
    }

    // --- format core -------------------------------------------------------
    inline void FormatTo(FormatBuffer& out, const char* fmt)
    {
        // No remaining args: copy the rest, honouring {{ and }} escapes.
        while (*fmt != '\0')
        {
            if (fmt[0] == '{' && fmt[1] == '{') { out.Append('{'); fmt += 2; }
            else if (fmt[0] == '}' && fmt[1] == '}') { out.Append('}'); fmt += 2; }
            else { out.Append(*fmt++); }
        }
    }

    template <typename T, typename... Rest>
    void FormatTo(FormatBuffer& out, const char* fmt, const T& value, const Rest&... rest)
    {
        while (*fmt != '\0')
        {
            if (fmt[0] == '{' && fmt[1] == '}')
            {
                AppendValue(out, value);
                FormatTo(out, fmt + 2, rest...);
                return;
            }
            if (fmt[0] == '{' && fmt[1] == '{') { out.Append('{'); fmt += 2; continue; }
            if (fmt[0] == '}' && fmt[1] == '}') { out.Append('}'); fmt += 2; continue; }
            out.Append(*fmt++);
        }
        // More args than `{}` placeholders: extra args are ignored.
    }
}
