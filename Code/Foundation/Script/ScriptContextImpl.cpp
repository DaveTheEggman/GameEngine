// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The current-script-context slot. Defined here - one per process per thread -
// because the pushers (script backends) and readers (native facades) live in
// different libraries; an inline thread_local in the interface would duplicate
// per shared library (shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"

module foundation.script;

namespace foundation::script
{
    ScriptBackendRegistry& ScriptBackendRegistry::Get()
    {
        static ScriptBackendRegistry instance;
        return instance;
    }
} // namespace foundation::script

namespace foundation::script::detail
{
    IScriptContext*& CurrentScriptContextSlot() noexcept
    {
        static thread_local IScriptContext* slot = nullptr;
        return slot;
    }
} // namespace foundation::script::detail
