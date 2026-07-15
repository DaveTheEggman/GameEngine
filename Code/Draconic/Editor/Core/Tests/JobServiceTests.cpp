// EditorJobService: a background job runs on a worker, reports through JobContext, and completes on
// the main thread from Update(). Tests pump Update() in a spin loop (the workers are fast) and check
// completion status, log drain, error propagation, and one-at-a-time ordering.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.editor.core;

using namespace draconic::core;
namespace ed = draconic::editor;

namespace
{
    // Pump Update() until no job is in flight (bounded, so a hang fails instead of spinning forever).
    void PumpUntilIdle(ed::EditorJobService& jobs, const Function<void(StringView)>& log = {})
    {
        int guard = 0;
        while (jobs.IsBusy() && guard++ < 2'000'000) { jobs.Update(log); }
        jobs.Update(log);   // final drain
    }
}

TEST_CASE("jobs: a submitted job runs on a worker, reports, and completes on Update")
{
    ed::EditorJobService jobs;

    bool doneFired = false;
    bool okStatus = false;
    jobs.Submit(u8"Work",
        [](ed::JobContext& ctx) -> Status
        {
            ctx.SetStep(u8"phase one", 1, 2);
            ctx.SetFraction(0.5f);
            ctx.Log(u8"halfway");
            ctx.SetStep(u8"phase two", 2, 2);
            ctx.SetFraction(1.0f);
            return Status{};
        },
        [&](Status s) { doneFired = true; okStatus = s.IsOk(); });

    CHECK(jobs.IsBusy());   // running synchronously spawned before Submit returned

    Array<String> logs;
    PumpUntilIdle(jobs, [&](StringView l) { logs.PushBack(String(l)); });

    CHECK(doneFired);
    CHECK(okStatus);
    CHECK_FALSE(jobs.IsBusy());
    CHECK_FALSE(jobs.Progress().active);   // idle => no active progress

    bool sawLog = false;
    for (const String& l : logs) { if (l == u8"halfway") { sawLog = true; } }
    CHECK(sawLog);
}

TEST_CASE("jobs: a failing job propagates its Status to onDone")
{
    ed::EditorJobService jobs;
    bool failed = false;
    jobs.Submit(u8"Bad",
        [](ed::JobContext&) -> Status { return Status{ ErrorCode::Internal }; },
        [&](Status s) { failed = !s.IsOk(); });
    PumpUntilIdle(jobs);
    CHECK(failed);
}

TEST_CASE("jobs: submissions run one at a time, in order")
{
    ed::EditorJobService jobs;
    Array<int> order;
    // The completion callbacks run on the main thread (from Update), so appending is race-free.
    for (int i = 0; i < 3; ++i)
    {
        jobs.Submit(u8"n", [](ed::JobContext&) -> Status { return Status{}; },
                    [&order, i](Status) { order.PushBack(i); });
    }
    PumpUntilIdle(jobs);
    REQUIRE(order.Size() == 3u);
    CHECK(order[0] == 0);
    CHECK(order[1] == 1);
    CHECK(order[2] == 2);
}
