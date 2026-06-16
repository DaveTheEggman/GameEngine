// Raptor Core — :serializer partition
//
// The serialization contract (§4.7): one direction-aware path serves both
// load and save. ISerializer is the pure interface; Serializer is the concrete
// base holding direction/version/sticky error.

module;
#include "Core/Prelude.h"

export module raptor.core:serializer;

import :base;

export namespace raptor::core
{
    enum class SerializeDirection
    {
        Load,
        Save,
    };

    // Pure interface: direction + the single raw-bytes primitive.
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        [[nodiscard]] virtual SerializeDirection Direction() const noexcept = 0;
        [[nodiscard]] virtual u32 Version() const noexcept = 0;

        // Moves `size` bytes between memory and the backing store, in whichever
        // direction this serializer runs.
        virtual Status SerializeRaw(void* data, usize size) = 0;

        [[nodiscard]] bool IsLoading() const noexcept { return Direction() == SerializeDirection::Load; }
        [[nodiscard]] bool IsSaving() const noexcept { return Direction() == SerializeDirection::Save; }
    };

    // Concrete base: holds direction, version, and a sticky error status.
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
