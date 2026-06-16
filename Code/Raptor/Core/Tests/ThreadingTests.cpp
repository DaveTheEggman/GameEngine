#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

// --- Threading -------------------------------------------------------------

TEST_CASE("threading: a thread runs and joins")
{
    Atomic<int> ran{ 0 };
    Thread t([&ran]() { ran.fetch_add(1); });
    CHECK(t.IsJoinable());
    t.Join();
    CHECK_FALSE(t.IsJoinable());
    CHECK(ran.load() == 1);
}

TEST_CASE("threading: Atomic fetch_add from many threads is exact")
{
    Atomic<i64> counter{ 0 };
    constexpr int kThreads = 8;
    constexpr int kPerThread = 10000;

    Array<Thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.PushBack(Thread([&counter]() {
            for (int j = 0; j < kPerThread; ++j) { counter.fetch_add(1); }
        }));
    }
    for (Thread& t : threads) { t.Join(); }

    CHECK(counter.load() == static_cast<i64>(kThreads) * kPerThread);
}

TEST_CASE("threading: Mutex/ScopedLock protects a non-atomic counter")
{
    Mutex mutex;
    i64 counter = 0;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 10000;

    Array<Thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.PushBack(Thread([&mutex, &counter]() {
            for (int j = 0; j < kPerThread; ++j)
            {
                ScopedLock lock(mutex);
                ++counter;
            }
        }));
    }
    for (Thread& t : threads) { t.Join(); }

    CHECK(counter == static_cast<i64>(kThreads) * kPerThread);
}

TEST_CASE("threading: ConditionVariable hand-off between two threads")
{
    Mutex mutex;
    ConditionVariable cv;
    bool ready = false;
    int payload = 0;

    Thread consumer([&]() {
        ScopedLock lock(mutex);
        while (!ready) { cv.Wait(mutex); }
        payload += 1; // observe the produced value
    });

    {
        ScopedLock lock(mutex);
        payload = 41;
        ready = true;
        cv.NotifyOne();
    }

    consumer.Join();
    CHECK(payload == 42);
}

TEST_CASE("threading: SpinLock protects a counter")
{
    SpinLock spin;
    i64 counter = 0;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 5000;

    Array<Thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.PushBack(Thread([&spin, &counter]() {
            for (int j = 0; j < kPerThread; ++j)
            {
                ScopedLock lock(spin);
                ++counter;
            }
        }));
    }
    for (Thread& t : threads) { t.Join(); }
    CHECK(counter == static_cast<i64>(kThreads) * kPerThread);
}

TEST_CASE("threading: Semaphore hands out a bounded number of permits")
{
    Semaphore sem(0);
    Atomic<int> acquired{ 0 };
    constexpr int kConsumers = 4;

    Array<Thread> threads;
    for (int i = 0; i < kConsumers; ++i)
    {
        threads.PushBack(Thread([&sem, &acquired]() {
            sem.Acquire();
            acquired.fetch_add(1);
        }));
    }

    // Release exactly one permit per consumer.
    for (int i = 0; i < kConsumers; ++i) { sem.Release(); }
    for (Thread& t : threads) { t.Join(); }

    CHECK(acquired.load() == kConsumers);
    CHECK_FALSE(sem.TryAcquire()); // none left
}

TEST_CASE("threading: SharedMutex allows shared reads and exclusive writes")
{
    SharedMutex rw;
    i64 value = 0;
    constexpr int kWriters = 4;
    constexpr int kPerWriter = 2000;

    Array<Thread> threads;
    for (int i = 0; i < kWriters; ++i)
    {
        threads.PushBack(Thread([&rw, &value]() {
            for (int j = 0; j < kPerWriter; ++j)
            {
                ScopedLock lock(rw); // exclusive
                ++value;
            }
        }));
    }
    // A reader that takes the shared lock a few times concurrently.
    threads.PushBack(Thread([&rw, &value]() {
        for (int j = 0; j < 1000; ++j)
        {
            ScopedSharedLock lock(rw);
            volatile i64 observed = value; // read under shared lock
            (void)observed;
        }
    }));

    for (Thread& t : threads) { t.Join(); }
    CHECK(value == static_cast<i64>(kWriters) * kPerWriter);
}

TEST_CASE("threading: JobSystem runs all enqueued jobs")
{
    JobSystem jobs(4);
    CHECK(jobs.WorkerCount() == 4u);

    Atomic<i64> sum{ 0 };
    constexpr int kJobs = 1000;
    for (int i = 0; i < kJobs; ++i)
    {
        jobs.Enqueue([&sum, i]() { sum.fetch_add(i); });
    }
    jobs.WaitForAll();

    i64 expected = 0;
    for (int i = 0; i < kJobs; ++i) { expected += i; }
    CHECK(sum.load() == expected);

    // WaitForAll with nothing pending returns immediately.
    jobs.WaitForAll();
    CHECK(sum.load() == expected);
}

TEST_CASE("threading: JobSystem default worker count is sane")
{
    JobSystem jobs;
    CHECK(jobs.WorkerCount() >= 1u);

    Atomic<int> done{ 0 };
    for (int i = 0; i < 50; ++i) { jobs.Enqueue([&done]() { done.fetch_add(1); }); }
    jobs.WaitForAll();
    CHECK(done.load() == 50);
}
