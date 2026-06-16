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

    // RAII exclusive-lock guard for any type with Lock()/Unlock() (Mutex,
    // SpinLock, SharedMutex). CTAD deduces the lockable: `ScopedLock lk(m);`.
    template <typename Lockable>
    class ScopedLock
    {
    public:
        explicit ScopedLock(Lockable& lockable) noexcept : m_lockable(&lockable) { m_lockable->Lock(); }
        ~ScopedLock() { m_lockable->Unlock(); }

        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        Lockable* m_lockable;
    };

    // =======================================================================
    // SpinLock — busy-wait exclusive lock for very short critical sections.
    // =======================================================================
    class SpinLock
    {
    public:
        void Lock() noexcept
        {
            while (m_locked.exchange(true, std::memory_order_acquire))
            {
                while (m_locked.load(std::memory_order_relaxed)) {} // spin without RMW
            }
        }

        [[nodiscard]] bool TryLock() noexcept
        {
            return !m_locked.exchange(true, std::memory_order_acquire);
        }

        void Unlock() noexcept { m_locked.store(false, std::memory_order_release); }

    private:
        std::atomic<bool> m_locked{ false };
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

    // =======================================================================
    // Semaphore — counting semaphore (Mutex + ConditionVariable).
    // =======================================================================
    class Semaphore
    {
    public:
        explicit Semaphore(i32 initialCount = 0) noexcept : m_count(initialCount) {}

        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        void Acquire() noexcept
        {
            ScopedLock lock(m_mutex);
            while (m_count == 0) { m_available.Wait(m_mutex); }
            --m_count;
        }

        [[nodiscard]] bool TryAcquire() noexcept
        {
            ScopedLock lock(m_mutex);
            if (m_count == 0) { return false; }
            --m_count;
            return true;
        }

        void Release() noexcept
        {
            ScopedLock lock(m_mutex);
            ++m_count;
            m_available.NotifyOne();
        }

    private:
        Mutex m_mutex;
        ConditionVariable m_available;
        i32 m_count;
    };

    // =======================================================================
    // SharedMutex — read/write lock (writer-preferring). Lock/Unlock for
    // exclusive (write); LockShared/UnlockShared for shared (read).
    // =======================================================================
    class SharedMutex
    {
    public:
        SharedMutex() noexcept = default;
        SharedMutex(const SharedMutex&) = delete;
        SharedMutex& operator=(const SharedMutex&) = delete;

        void Lock() noexcept // exclusive
        {
            ScopedLock lock(m_mutex);
            ++m_writersWaiting;
            while (m_writeActive || m_readers > 0) { m_gate.Wait(m_mutex); }
            --m_writersWaiting;
            m_writeActive = true;
        }

        void Unlock() noexcept
        {
            ScopedLock lock(m_mutex);
            m_writeActive = false;
            m_gate.NotifyAll();
        }

        void LockShared() noexcept // read
        {
            ScopedLock lock(m_mutex);
            while (m_writeActive || m_writersWaiting > 0) { m_gate.Wait(m_mutex); }
            ++m_readers;
        }

        void UnlockShared() noexcept
        {
            ScopedLock lock(m_mutex);
            if (--m_readers == 0) { m_gate.NotifyAll(); }
        }

    private:
        Mutex m_mutex;
        ConditionVariable m_gate;
        i32 m_readers = 0;
        i32 m_writersWaiting = 0;
        bool m_writeActive = false;
    };

    // RAII shared-lock guard for SharedMutex.
    class ScopedSharedLock
    {
    public:
        explicit ScopedSharedLock(SharedMutex& mutex) noexcept : m_mutex(&mutex) { m_mutex->LockShared(); }
        ~ScopedSharedLock() { m_mutex->UnlockShared(); }

        ScopedSharedLock(const ScopedSharedLock&) = delete;
        ScopedSharedLock& operator=(const ScopedSharedLock&) = delete;

    private:
        SharedMutex* m_mutex;
    };
}
