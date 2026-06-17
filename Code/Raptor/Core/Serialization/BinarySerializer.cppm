// Raptor Core — :binary_serializer partition
//
// Raw binary backend (as-stored bytes) over an IStream. Names and object scopes
// carry no information here, so they are dropped; arrays and strings store a
// u32 count/length prefix, and scalars store their natural width.

module;
#include "Core/Prelude.h"

export module raptor.core:binary_serializer;

import :base;
import :serializer;
import :string;
import :io;

export namespace raptor::core
{
    // Backend: raw little-endian-as-stored binary over an IStream.
    class BinarySerializer final : public Serializer
    {
    public:
        BinarySerializer(IStream& stream, SerializeMode mode) noexcept
            : Serializer(mode), m_stream(&stream) {}

        // Naming and object scopes are inherited as no-ops from Serializer; a
        // flat byte stream carries no names. Arrays are length-prefixed.
        void BeginArray(u32& count) override { Scalar(&count, ScalarKind::UInt32); }

        void Scalar(void* value, ScalarKind kind) override
        {
            RawBytes(value, ScalarSize(kind));
        }

        void Text(String& value) override
        {
            u32 length = static_cast<u32>(value.Size());
            Scalar(&length, ScalarKind::UInt32);

            if (IsReading())
            {
                value.Clear();
                value.Reserve(length);
                for (u32 i = 0; i < length; ++i)
                {
                    widechar ch{};
                    RawBytes(&ch, sizeof(widechar));
                    value.PushBack(ch);
                }
            }
            else
            {
                for (u32 i = 0; i < length; ++i)
                {
                    widechar ch = value[i];
                    RawBytes(&ch, sizeof(widechar));
                }
            }
        }

        void Blob(void* data, usize size) override { RawBytes(data, size); }

    private:
        // Moves `size` bytes in whichever direction this serializer runs.
        void RawBytes(void* data, usize size)
        {
            if (size == 0) { return; }

            if (IsWriting())
            {
                if (m_stream->Write(data, size) != size) { Fail(ErrorCode::Internal); }
            }
            else
            {
                if (m_stream->Read(data, size) != size) { Fail(ErrorCode::Internal); }
            }
        }

        static usize ScalarSize(ScalarKind kind) noexcept
        {
            switch (kind)
            {
                case ScalarKind::Bool:    return sizeof(bool);
                case ScalarKind::Int8:    case ScalarKind::UInt8:    return 1;
                case ScalarKind::Int16:   case ScalarKind::UInt16:   return 2;
                case ScalarKind::Int32:   case ScalarKind::UInt32:
                case ScalarKind::Float32:                            return 4;
                case ScalarKind::Int64:   case ScalarKind::UInt64:
                case ScalarKind::Float64:                            return 8;
            }
            return 0;
        }

        IStream* m_stream;
    };
}
