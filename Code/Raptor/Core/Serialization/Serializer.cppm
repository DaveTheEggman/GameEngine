// Raptor Core — :serializer partition
//
// Serializer: the concrete base over ISerializer that holds direction, version,
// and a sticky error Status. Backends (e.g. BinarySerializer) extend this.

module;
#include "Core/Prelude.h"

export module raptor.core:serializer;

export import :iserializer;
import :base;

export namespace raptor::core
{
    // Backends extend this, not ISerializer directly.
    class Serializer : public ISerializer
    {
    public:
        explicit Serializer(SerializeDirection direction) noexcept : m_direction(direction) {}

        [[nodiscard]] SerializeDirection Direction() const noexcept override { return m_direction; }
        [[nodiscard]] u32 Version() const noexcept override { return m_version; }
        void SetVersion(u32 version) noexcept { m_version = version; }

        [[nodiscard]] Status GetStatus() const noexcept { return m_status; }
        [[nodiscard]] bool IsOk() const noexcept { return m_status.IsOk(); }

    protected:
        void Fail(ErrorCode code) noexcept
        {
            if (m_status.IsOk()) { m_status = code; }
        }

        SerializeDirection m_direction;
        u32 m_version = 0;
        Status m_status{};
    };
}
