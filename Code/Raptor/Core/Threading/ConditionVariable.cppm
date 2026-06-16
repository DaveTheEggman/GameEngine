// Raptor Core — :condition_variable partition

module;
#include "Core/Prelude.h"
#include "Core/Threading/ThreadBackend.h"

export module raptor.core:condition_variable;

import :base;
import :mutex;

export namespace raptor::core
{
    class ConditionVariable
    {
    public:
        ConditionVariable() noexcept { sys::CondInit(&m_storage); }
        ~ConditionVariable() { sys::CondDestroy(&m_storage); }

        ConditionVariable(const ConditionVariable&) = delete;
        ConditionVariable& operator=(const ConditionVariable&) = delete;

        // The mutex must be held by the caller; it is released while waiting.
        void Wait(Mutex& mutex) noexcept { sys::CondWait(&m_storage, mutex.NativeHandle()); }
        void NotifyOne() noexcept { sys::CondSignal(&m_storage); }
        void NotifyAll() noexcept { sys::CondBroadcast(&m_storage); }

    private:
        alignas(sys::kCondStorageAlign) byte m_storage[sys::kCondStorageSize];
    };
}
