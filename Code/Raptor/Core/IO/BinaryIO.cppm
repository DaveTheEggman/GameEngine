// Raptor Core — :binary_io partition
//
// BinaryWriter / BinaryReader: thin typed wrappers over an IStream for raw
// binary I/O. They track a sticky "ok" flag (set false on a short transfer) so
// callers can do a batch of reads/writes and check success once at the end.
// BinarySerializer is built on these.

module;
#include "Core/Prelude.h"
#include <type_traits>

export module raptor.core:binary_io;

import :base;
import :string;
import :io;

export namespace raptor::core
{
    // =======================================================================
    // BinaryWriter — typed binary output over an IStream.
    // =======================================================================
    class BinaryWriter
    {
    public:
        explicit BinaryWriter(IStream& stream) noexcept : m_stream(&stream) {}

        [[nodiscard]] bool IsOk() const noexcept { return m_ok; }

        // Raw bytes. Sticks to !IsOk() on a short write.
        bool WriteBytes(const void* data, usize size)
        {
            if (size == 0) { return m_ok; }
            if (m_stream->Write(data, size) != size) { m_ok = false; }
            return m_ok;
        }

        // One trivially-copyable value, as-stored.
        template <typename T>
        bool Write(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>, "BinaryWriter::Write requires a trivially-copyable type.");
            return WriteBytes(&value, sizeof(T));
        }

        // Length-prefixed (u32 count) wide-character string.
        bool WriteString(WideStringView value)
        {
            const u32 length = static_cast<u32>(value.Size());
            Write(length);
            if (length > 0) { WriteBytes(value.Data(), static_cast<usize>(length) * sizeof(widechar)); }
            return m_ok;
        }

    private:
        IStream* m_stream;
        bool m_ok = true;
    };

    // =======================================================================
    // BinaryReader — typed binary input over an IStream.
    // =======================================================================
    class BinaryReader
    {
    public:
        explicit BinaryReader(IStream& stream) noexcept : m_stream(&stream) {}

        [[nodiscard]] bool IsOk() const noexcept { return m_ok; }

        // Raw bytes. Sticks to !IsOk() on a short read.
        bool ReadBytes(void* data, usize size)
        {
            if (size == 0) { return m_ok; }
            if (m_stream->Read(data, size) != size) { m_ok = false; }
            return m_ok;
        }

        // One trivially-copyable value, as-stored.
        template <typename T>
        bool Read(T& outValue)
        {
            static_assert(std::is_trivially_copyable_v<T>, "BinaryReader::Read requires a trivially-copyable type.");
            return ReadBytes(&outValue, sizeof(T));
        }

        // Length-prefixed (u32 count) wide-character string.
        bool ReadString(WideString& outValue)
        {
            u32 length = 0;
            Read(length);
            outValue.Clear();
            if (!m_ok) { return false; }

            outValue.Reserve(length);
            for (u32 i = 0; i < length; ++i)
            {
                widechar ch{};
                if (!ReadBytes(&ch, sizeof(widechar))) { break; }
                outValue.PushBack(ch);
            }
            return m_ok;
        }

    private:
        IStream* m_stream;
        bool m_ok = true;
    };
}
