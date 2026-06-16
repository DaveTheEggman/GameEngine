// Raptor Core — :job_system partition
//
// Fixed worker pool that runs enqueued callables; WaitForAll joins them.

module;
#include "Core/Prelude.h"
#include <atomic>

export module raptor.core:job_system;

import :base;
import :allocator;
import :array;
import :system;
import :thread;
import :mutex;
import :condition_variable;
import :scoped_lock;
import :atomic;

export namespace raptor::core
{
    class JobSystem
    {
    public:
        // workerCount == 0 picks (logical cores - 1), at least 1.
        explicit JobSystem(u32 workerCount = 0)
        {
            u32 count = workerCount;
            if (count == 0)
            {
                const u32 cores = LogicalCoreCount();
                count = (cores > 1) ? (cores - 1) : 1;
            }
            for (u32 i = 0; i < count; ++i)
            {
                m_workers.PushBack(Thread([this]() { WorkerLoop(); }));
            }
        }

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        ~JobSystem()
        {
            {
                ScopedLock lock(m_mutex);
                m_stop = true;
            }
            m_jobAvailable.NotifyAll();
            for (Thread& worker : m_workers) { worker.Join(); }
        }

        [[nodiscard]] u32 WorkerCount() const noexcept { return static_cast<u32>(m_workers.Size()); }

        template <typename Fn>
        void Enqueue(Fn function)
        {
            Fn* held = DefaultAllocator().New<Fn>(Move(function));
            const Job job{
                [](void* p) { (*static_cast<Fn*>(p))(); },
                [](void* p) { DefaultAllocator().Delete(static_cast<Fn*>(p)); },
                held
            };

            m_pending.fetch_add(1, std::memory_order_relaxed);
            {
                ScopedLock lock(m_mutex);
                m_queue.PushBack(job);
            }
            m_jobAvailable.NotifyOne();
        }

        // Blocks until all enqueued jobs have completed.
        void WaitForAll()
        {
            ScopedLock lock(m_mutex);
            while (m_pending.load(std::memory_order_acquire) != 0)
            {
                m_allDone.Wait(m_mutex);
            }
        }

    private:
        struct Job
        {
            void (*invoke)(void*);
            void (*destroy)(void*);
            void* data;
        };

        void WorkerLoop()
        {
            for (;;)
            {
                Job job{};
                {
                    ScopedLock lock(m_mutex);
                    while (m_queue.IsEmpty() && !m_stop) { m_jobAvailable.Wait(m_mutex); }
                    if (m_queue.IsEmpty()) { return; } // stop requested and drained
                    job = m_queue.Back();
                    m_queue.PopBack();
                }

                job.invoke(job.data);
                job.destroy(job.data);

                if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    ScopedLock lock(m_mutex);
                    m_allDone.NotifyAll();
                }
            }
        }

        Array<Job> m_queue;
        Mutex m_mutex;
        ConditionVariable m_jobAvailable;
        ConditionVariable m_allDone;
        Array<Thread> m_workers;
        Atomic<i64> m_pending{ 0 };
        bool m_stop = false;
    };
}
