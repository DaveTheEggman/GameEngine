// Raptor Core — :threading partition
//
// Threads and synchronization on the platform backend. Atomic is the language
// <atomic>. Mutex/ConditionVariable wrap opaque OS storage. The job/task system
// is a later deliverable.
//
// NOTE: the global Logger and DefaultAllocator are not yet thread-safe; guard
// shared state with these primitives until those are made safe.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include "Core/Threading/ThreadBackend.h"
#include <atomic>

export module raptor.core:threading;

import :base;
import :memory;

export namespace raptor::core
{
    template <typename T>
    using Atomic = std::atomic<T>;

    // =======================================================================
    // Thread — owns an OS thread running a callable. Join or Detach before
    // destruction (asserts otherwise).
    // =======================================================================
    class Thread
    {
    public:
        Thread() noexcept = default;

        template <typename Fn>
        explicit Thread(Fn function)
        {
            Fn* held = DefaultAllocator().New<Fn>(Move(function));
            m_handle = sys::ThreadCreate(&ThreadEntry<Fn>, held);
            m_joinable = (m_handle != sys::kInvalidThread);
            if (!m_joinable)
            {
                DefaultAllocator().Delete(held);
            }
        }

        Thread(Thread&& other) noexcept : m_handle(other.m_handle), m_joinable(other.m_joinable)
        {
            other.m_handle = sys::kInvalidThread;
            other.m_joinable = false;
        }

        Thread& operator=(Thread&& other) noexcept
        {
            if (this != &other)
            {
                RAPTOR_ASSERT_MSG(!m_joinable, "Thread overwritten while still joinable");
                m_handle = other.m_handle;
                m_joinable = other.m_joinable;
                other.m_handle = sys::kInvalidThread;
                other.m_joinable = false;
            }
            return *this;
        }

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        ~Thread()
        {
            if (m_joinable)
            {
                RAPTOR_ASSERT_MSG(false, "Thread destroyed while still joinable; call Join or Detach");
                sys::ThreadDetach(m_handle);
            }
        }

        [[nodiscard]] bool IsJoinable() const noexcept { return m_joinable; }

        void Join() noexcept
        {
            if (m_joinable)
            {
                sys::ThreadJoin(m_handle);
                m_joinable = false;
            }
        }

        void Detach() noexcept
        {
            if (m_joinable)
            {
                sys::ThreadDetach(m_handle);
                m_joinable = false;
            }
        }

        [[nodiscard]] static u64 CurrentId() noexcept { return sys::CurrentThreadId(); }

    private:
        template <typename Fn>
        static void ThreadEntry(void* arg)
        {
            Fn* function = static_cast<Fn*>(arg);
            (*function)();
            DefaultAllocator().Delete(function);
        }

        sys::ThreadHandle m_handle = sys::kInvalidThread;
        bool m_joinable = false;
    };

    // =======================================================================
    // Mutex / ScopedLock
    // =======================================================================
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

    // RAII lock guard.
    class ScopedLock
    {
    public:
        explicit ScopedLock(Mutex& mutex) noexcept : m_mutex(&mutex) { m_mutex->Lock(); }
        ~ScopedLock() { m_mutex->Unlock(); }

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        Mutex* m_mutex;
    };

    // =======================================================================
    // ConditionVariable
    // =======================================================================
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
