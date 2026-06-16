// Raptor Core — :binary_serializer partition
//
// Raw binary serializer (as-stored bytes) over an IStream.

module;
#include "Core/Prelude.h"

export module raptor.core:binary_serializer;

import :base;
import :serializer;
import :io;

export namespace raptor::core
{
    // Backend: raw little-endian-as-stored binary over an IStream.
    class BinarySerializer final : public Serializer
    {
    public:
        BinarySerializer(IStream& stream, SerializeDirection direction) noexcept
            : Serializer(direction), m_stream(&stream) {}

        Status SerializeRaw(void* data, usize size) override
        {
            if (size == 0) { return Status{}; }

            if (IsSaving())
            {
                if (m_stream->Write(data, size) != size) { Fail(ErrorCode::Internal); }
            }
            else
            {
                if (m_stream->Read(data, size) != size) { Fail(ErrorCode::Internal); }
            }
            return m_status;
        }

    private:
        IStream* m_stream;
    };
}
