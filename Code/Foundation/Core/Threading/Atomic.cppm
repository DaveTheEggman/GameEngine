// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Core - :atomic partition
//
// Atomic<T> aliases the language <atomic>, and MemoryOrder names its orderings, so a module
// outside Core never includes <atomic> itself (GCC 15 cannot read back a module whose partition
// does, "Bad file data").

module;
#include "Core/Prelude.h"
#include <atomic>

export module foundation.core:atomic;

export namespace foundation::core
{
    template <typename T>
    using Atomic = std::atomic<T>;

    /// The orderings an Atomic's operations take: x.load(MemoryOrder::Acquire).
    struct MemoryOrder
    {
        static constexpr std::memory_order Relaxed = std::memory_order_relaxed;
        static constexpr std::memory_order Acquire = std::memory_order_acquire;
        static constexpr std::memory_order Release = std::memory_order_release;
        static constexpr std::memory_order AcqRel = std::memory_order_acq_rel;
        static constexpr std::memory_order SeqCst = std::memory_order_seq_cst;
    };
}
