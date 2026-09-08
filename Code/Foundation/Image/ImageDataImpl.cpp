// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Process-wide Image state: the ImageData instance-id counter (see ImageData::InstanceId).
module;
#include "Core/Prelude.h"
#include <atomic>

module foundation.image;

import foundation.core;

namespace foundation::image
{
    core::u64 ImageData::NextInstanceId()
    {
        static std::atomic<core::u64> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }
}
