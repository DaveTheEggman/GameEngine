// Raptor Core — :iserializer partition
//
// The serialization contract: a direction-aware pure interface. One Serialize
// path serves both load and save; SerializeRaw is the single primitive.

module;
#include "Core/Prelude.h"

export module raptor.core:iserializer;

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
}
