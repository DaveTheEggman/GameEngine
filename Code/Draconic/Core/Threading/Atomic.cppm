// Draconic Core - :atomic partition
//
// Atomic<T> aliases the language <atomic>.

module;
#include "Core/Prelude.h"
#include <atomic>

export module draconic.core:atomic;

export namespace draconic::core
{
    template <typename T>
    using Atomic = std::atomic<T>;
}
