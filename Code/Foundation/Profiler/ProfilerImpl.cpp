// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The profiler singleton + per-thread slot. Defined here - one per process -
// because PROFILE_SCOPE expands in 17+ libraries; inline definitions in the
// interface would give each shared library its own profiler and thread slots
// (shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"

module foundation.profiler;

namespace foundation::profiler
{
    Profiler& Profiler::Get() noexcept
    {
        static Profiler instance;
        return instance;
    }

    Profiler::ThreadData*& Profiler::LocalSlot() noexcept
    {
        static thread_local ThreadData* slot = nullptr;
        return slot;
    }
} // namespace foundation::profiler
