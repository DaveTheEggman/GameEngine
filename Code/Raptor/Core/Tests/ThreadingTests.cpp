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
