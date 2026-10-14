// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The process's capture recorder. Defined here - one per process - because the key, the flag and
// the tools that start a capture live in different libraries (shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"

module foundation.profiler.capture;

import foundation.core;

using namespace foundation::core;

namespace foundation::profiler
{
    CaptureRecorder& CaptureRecorder::Get() noexcept
    {
        // Process-root decision: the process's recorder lives on the system allocator.
        static CaptureRecorder instance(DefaultAllocator());
        return instance;
    }
} // namespace foundation::profiler
