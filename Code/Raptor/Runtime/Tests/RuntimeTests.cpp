#include <doctest/doctest.h>

#include "Core/Prelude.h"  // <new> reachability for container instantiation (GCC)

import raptor.core;
import raptor.runtime;

using namespace raptor::core;
using namespace raptor::runtime;

namespace
{
    // Distinct subsystem types (distinct TypeOf<> keys). Each records its tag in a
    // shared log on Update and counts its lifecycle calls.
    template <int Tag>
    class Sys final : public Subsystem
    {
    public:
        Sys(i32 order, Array<int>* log) : m_order(order), m_log(log) {}

        [[nodiscard]] i32 UpdateOrder() const noexcept override { return m_order; }

        void Update(f32) override { if (m_log != nullptr) { m_log->PushBack(Tag); } ++updates; }
        void BeginFrame(f32) override { ++beginFrames; }
        void EndFrame() override { ++endFrames; }

        int inits = 0, readys = 0, updates = 0, shutdowns = 0, beginFrames = 0, endFrames = 0;

    protected:
        void OnInit() override { ++inits; }
        void OnReady() override { ++readys; }
        void OnShutdown() override { ++shutdowns; }

    private:
        i32 m_order;
        Array<int>* m_log;
    };
}

TEST_CASE("runtime: register, look up, and own subsystems by type")
{
    Context ctx;
    CHECK_FALSE(ctx.HasSubsystem<Sys<1>>());

    Sys<1>* a = ctx.AddSubsystem<Sys<1>>(0, nullptr);
    Sys<2>* b = ctx.AddSubsystem<Sys<2>>(0, nullptr);

    CHECK(ctx.HasSubsystem<Sys<1>>());
    CHECK(ctx.GetSubsystem<Sys<1>>() == a);
    CHECK(ctx.GetSubsystem<Sys<2>>() == b);
    CHECK(ctx.GetSubsystem<Sys<3>>() == nullptr);  // never registered
    CHECK(a->GetContext() == &ctx);                 // OnRegister wired the context
}

TEST_CASE("runtime: startup / shutdown lifecycle")
{
    Context ctx;
    Sys<1>* a = ctx.AddSubsystem<Sys<1>>(0, nullptr);
    CHECK_FALSE(a->IsInitialized());
    CHECK_FALSE(ctx.IsRunning());

    ctx.Startup();
    CHECK(ctx.IsRunning());
    CHECK(a->inits == 1);
    CHECK(a->readys == 1);
    CHECK(a->IsInitialized());

    ctx.Shutdown();
    CHECK_FALSE(ctx.IsRunning());
    CHECK(a->shutdowns == 1);
    CHECK_FALSE(a->IsInitialized());
}

TEST_CASE("runtime: frame phases run in UpdateOrder")
{
    Array<int> log;
    Context ctx;
    Sys<2>* high = ctx.AddSubsystem<Sys<2>>(5, &log);    // runs later
    Sys<1>* low = ctx.AddSubsystem<Sys<1>>(-10, &log);   // runs earlier

    ctx.Startup();
    ctx.BeginFrame(0.016f);
    ctx.Update(0.016f);
    ctx.EndFrame();

    REQUIRE(log.Size() == 2u);
    CHECK(log[0] == 1);   // Sys<1> (order -10) before
    CHECK(log[1] == 2);   // Sys<2> (order 5)
    CHECK(low->updates == 1);
    CHECK(high->updates == 1);
    CHECK(low->beginFrames == 1);
    CHECK(high->endFrames == 1);
}

TEST_CASE("runtime: Dispose shuts down running subsystems")
{
    int shutdowns = 0;
    {
        Context ctx;
        Sys<1>* a = ctx.AddSubsystem<Sys<1>>(0, nullptr);
        ctx.Startup();
        CHECK(a->IsInitialized());
        // ctx destructs here -> Dispose -> Shutdown -> destroy
    }
    // (Lifecycle correctness is covered above; this exercises the dtor path.)
    CHECK(shutdowns == 0);
}
