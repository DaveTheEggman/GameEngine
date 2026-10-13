// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// The process-global JobSystem pointer and the per-thread worker slot. Defined in
// Core's implementation unit - NON-inline - so exactly one of each exists per
// process; inline definitions in the interface would duplicate per shared library
// (shared-libraries.md rendezvous rule).

module;
#include "Core/Prelude.h"
#include <atomic>

module foundation.core;

namespace foundation::core
{
    namespace
    {
        JobSystem* g_globalJobs = nullptr;
        std::atomic<const JobHooks*> g_jobHooks{nullptr};
    }

    void SetJobHooks(const JobHooks* hooks) noexcept { g_jobHooks.store(hooks, std::memory_order_release); }

    const JobHooks* CurrentJobHooks() noexcept { return g_jobHooks.load(std::memory_order_acquire); }

    i32& JobSystem::WorkerSlot() noexcept
    {
        static thread_local i32 slot = -1;
        return slot;
    }

    void InitGlobalJobSystem(u32 workerCount)
    {
        if (g_globalJobs == nullptr)
        {
            // Process-root decision: the engine-wide pool lives on the system allocator
            // (this accessor pair IS a composition root, like DefaultAllocator itself).
            g_globalJobs = DefaultAllocator().New<JobSystem>(DefaultAllocator(), workerCount);
        }
    }

    void ShutdownGlobalJobSystem()
    {
        if (g_globalJobs != nullptr)
        {
            DefaultAllocator().Delete(g_globalJobs);
            g_globalJobs = nullptr;
        }
    }

    bool HasGlobalJobSystem() noexcept { return g_globalJobs != nullptr; }
    JobSystem& GlobalJobs() noexcept { return *g_globalJobs; }
} // namespace foundation::core
