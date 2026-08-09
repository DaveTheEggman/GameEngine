// Core - :atomic partition
//
// Atomic<T> aliases the language <atomic>.

module;
#include "Core/Prelude.h"
#include <atomic>

export module foundation.core:atomic;

export namespace foundation::core
{
    template <typename T>
    using Atomic = std::atomic<T>;
}
