// Draconic Core - :binary_serializer partition
//
// Raw binary backend (as-stored bytes) over an IStream, built on BinaryReader /
// BinaryWriter. Names and object scopes carry no information here, so they are
// dropped; arrays and strings store a u32 count/length prefix, and scalars
// store their natural width.

module;
#include "Draconic.Core/Prelude.h"

export module draconic.core:binary_serializer;

import :base;
import :serializer;
import :string;
import :guid;
import :io;
import :binary_io;
import :unique_ptr;
import :allocator;
import :function;

export namespace draconic::core
{
    // Backend: raw little-endian-as-stored binary over an IStream.
    class BinarySerializer final : public Serializer
    {
    public:
        BinarySerializer(IStream& stream, SerializeMode mode) noexcept
            : Serializer(mode), m_reader(stream), m_writer(stream)
        {
        }

        // Naming and object scopes are inherited as no-ops from Serializer; a
        // flat byte stream carries no names. Arrays are length-prefixed.
        void BeginArray(u32& count) override { Scalar(&count, ScalarKind::UInt32); }

        void Scalar(void* value, ScalarKind kind) override { RawBytes(value, ScalarSize(kind)); }

        void Text(String& value) override
        {
            if (IsWriting())
            {
                if (!m_writer.WriteString(value))
                {
                    Fail(ErrorCode::Internal);
                }
            }
            else
            {
                if (!m_reader.ReadString(value))
                {
                    Fail(ErrorCode::Internal);
                }
            }
        }

        void Blob(void* data, usize size) override { RawBytes(data, size); }

        // Compact: the raw 16 bytes (two u64 halves), not the canonical string. (Traktor-style.)
        void GuidValue(Guid& value) override { RawBytes(&value, sizeof(Guid)); }

    private:
        // Moves `size` bytes in whichever direction this serializer runs.
        void RawBytes(void* data, usize size)
        {
            if (size == 0)
            {
                return;
            }

            if (IsWriting())
            {
                if (!m_writer.WriteBytes(data, size))
                {
                    Fail(ErrorCode::Internal);
                }
            }
            else
            {
                if (!m_reader.ReadBytes(data, size))
                {
                    Fail(ErrorCode::Internal);
                }
            }
        }

        static usize ScalarSize(ScalarKind kind) noexcept
        {
            switch (kind)
            {
            case ScalarKind::Bool:
                return sizeof(bool);
            case ScalarKind::Int8:
            case ScalarKind::UInt8:
                return 1;
            case ScalarKind::Int16:
            case ScalarKind::UInt16:
                return 2;
            case ScalarKind::Int32:
            case ScalarKind::UInt32:
            case ScalarKind::Float32:
                return 4;
            case ScalarKind::Int64:
            case ScalarKind::UInt64:
            case ScalarKind::Float64:
                return 8;
            }
            return 0;
        }

        BinaryReader m_reader;
        BinaryWriter m_writer;
    };

    // Built-in SerializerFactory for binary serialization.
    namespace detail
    {
        struct BinarySerializerContext final : SerializerContext
        {
            BinarySerializer impl;
            BinarySerializerContext(IStream& stream, SerializeMode mode) : impl(stream, mode)
            {
                serializer = &impl;
            }
        };
    }

    [[nodiscard]] inline SerializerFactory BinarySerializerFactory()
    {
        return SerializerFactory{
            [](IStream& stream, SerializeMode mode) -> UniquePtr<SerializerContext>
            {
                return MakeUnique<detail::BinarySerializerContext>(DefaultAllocator(), stream,
                                                                   mode);
            }};
    }
}
