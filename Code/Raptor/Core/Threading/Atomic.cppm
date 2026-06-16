// Raptor Core — :atomic partition
//
// Atomic<T> aliases the language <atomic>.

module;
#include "Core/Prelude.h"
#include <atomic>

export module raptor.core:atomic;


export namespace raptor::core
{
    template <typename T>
    using Atomic = std::atomic<T>;
}
