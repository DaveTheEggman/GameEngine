// Draconic Core - :mutex partition
//
// Mutex over opaque OS storage.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Threading/ThreadBackend.h"

export module draconic.core:mutex;

import :base;

export namespace draconic::core
{
    class Mutex
    {
    public:
        Mutex() noexcept { sys::MutexInit(&m_storage); }
        ~Mutex() { sys::MutexDestroy(&m_storage); }

        Mutex(const Mutex&) = delete;
        Mutex& operator=(const Mutex&) = delete;

        void Lock() noexcept { sys::MutexLock(&m_storage); }
        [[nodiscard]] bool TryLock() noexcept { return sys::MutexTryLock(&m_storage); }
        void Unlock() noexcept { sys::MutexUnlock(&m_storage); }

        // For ConditionVariable; not for general use.
        [[nodiscard]] void* NativeHandle() noexcept { return &m_storage; }

    private:
        alignas(sys::kMutexStorageAlign) byte m_storage[sys::kMutexStorageSize];
    };
}
