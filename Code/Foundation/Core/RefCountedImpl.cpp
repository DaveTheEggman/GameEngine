// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The MakeRef -> RefCounted-ctor handshake slot. Defined here - in Core's one
// implementation unit - so the whole process shares a single slot per thread:
// MakeRef<T> instantiates into the CALLING library while the constructed type may
// live in another, and an inline thread_local would duplicate per shared library,
// leaving m_control null on every object built across a boundary
// (shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"

module foundation.core;

namespace foundation::core::detail
{
    RefControl*& PendingRefControl() noexcept
    {
        static thread_local RefControl* slot = nullptr;
        return slot;
    }
} // namespace foundation::core::detail
