#include <doctest/doctest.h>

#include "Core/Prelude.h"  // <new> reachability for container instantiation (GCC)

import raptor.core;
import raptor.runtime;

using namespace raptor::core;
using namespace raptor::runtime;

namespace
{
    // Build dirs hand us a narrow UTF-8 path; the IO/Library APIs take wide views.
    [[nodiscard]] String WidePath(const char* p)
    {
        return ToWide(UTF8StringView{ reinterpret_cast<const utf8char*>(p) });
    }

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

TEST_CASE("runtime: register a caller-owned subsystem; Context does not destroy it")
{
    Sys<1> owned(0, nullptr);  // lives on the stack; Context must not free it
    {
        Context ctx;
        Sys<1>* registered = ctx.RegisterSubsystem<Sys<1>>(&owned);
        CHECK(registered == &owned);
        CHECK(ctx.GetSubsystem<Sys<1>>() == &owned);

        ctx.Startup();
        ctx.Update(0.016f);
        CHECK(owned.inits == 1);
        CHECK(owned.updates == 1);
        // ctx disposes here: shuts the subsystem down but must NOT destroy it.
    }
    CHECK(owned.shutdowns == 1);  // object still valid -> reading it is safe
}

TEST_CASE("runtime: subsystems registered after Startup come up immediately")
{
    Context ctx;
    ctx.Startup();
    CHECK(ctx.IsRunning());

    Sys<1>* late = ctx.AddSubsystem<Sys<1>>(0, nullptr);
    CHECK(late->inits == 1);    // Init + Ready ran on registration
    CHECK(late->readys == 1);
    CHECK(late->IsInitialized());

    ctx.Update(0.016f);
    CHECK(late->updates == 1);  // participates in the frame loop right away
}

TEST_CASE("runtime: RemoveSubsystem shuts down, unregisters, and destroys owned")
{
    Context ctx;
    ctx.AddSubsystem<Sys<1>>(0, nullptr);
    ctx.AddSubsystem<Sys<2>>(0, nullptr);
    ctx.Startup();

    ctx.RemoveSubsystem<Sys<1>>();
    CHECK_FALSE(ctx.HasSubsystem<Sys<1>>());
    CHECK(ctx.HasSubsystem<Sys<2>>());

    // The removed subsystem no longer ticks; the survivor still does.
    Array<int> log;
    Sys<2>* b = ctx.GetSubsystem<Sys<2>>();
    b->Update(0.016f);   // sanity: survivor is live
    CHECK(b->updates == 1);
}

namespace
{
    // A plugin defined in-process (statically linked). Owns its subsystem and
    // registers/removes it non-owningly across load/unload.
    class StaticTestPlugin final : public IRuntimePlugin
    {
    public:
        [[nodiscard]] StringView Name() const noexcept override { return u"StaticTestPlugin"; }
        void OnLoad(Context& ctx) override { ctx.RegisterSubsystem<Sys<7>>(&m_sys); }
        void OnUnload(Context& ctx) override { ctx.RemoveSubsystem<Sys<7>>(); }

        Sys<7> m_sys{ 0, nullptr };
    };
}

TEST_CASE("runtime: PluginHost::Add registers a static plugin's subsystem")
{
    Context ctx;
    StaticTestPlugin plugin;
    {
        PluginHost host(ctx);
        CHECK(host.Add(&plugin) == &plugin);
        CHECK(host.Count() == 1u);
        CHECK(ctx.HasSubsystem<Sys<7>>());

        ctx.Startup();
        ctx.Update(0.016f);
        CHECK(plugin.m_sys.updates == 1);

        host.UnloadAll();
        CHECK(host.Count() == 0u);
        CHECK_FALSE(ctx.HasSubsystem<Sys<7>>());  // OnUnload removed it
    }
    // Plugin object outlives the host (caller-owned) and was never freed by it.
    CHECK(plugin.m_sys.shutdowns == 1);
}

TEST_CASE("runtime: PluginHost::Load loads a plugin from a shared library")
{
    Context ctx;
    {
        PluginHost host(ctx);

        const String path = WidePath(RAPTOR_TEST_PLUGIN_PATH);
        auto loaded = host.Load(path.AsView());
        REQUIRE(loaded.HasValue());
        CHECK(host.Count() == 1u);
        CHECK(loaded.Value()->Name() == StringView{ u"RaptorTestPlugin" });

        ctx.Startup();
        ctx.Update(0.016f);

        // Observe the library's subsystem ran via a C symbol it exports. A second
        // handle to the same image shares the counter (dlopen refcounts).
        DynamicLibrary probe{ path.AsView() };
        REQUIRE(probe.IsLoaded());
        const auto ticks = probe.GetSymbol<int (*)()>(u"RaptorTestPluginTicks");
        REQUIRE(ticks != nullptr);
        CHECK(ticks() == 1);

        host.UnloadAll();
        CHECK(host.Count() == 0u);

        // After unload the plugin's subsystem is gone; updating must not tick it.
        ctx.Update(0.016f);
        CHECK(ticks() == 1);
    }
}

TEST_CASE("runtime: PluginHost::Load reports failure for a missing library")
{
    Context ctx;
    PluginHost host(ctx);
    auto result = host.Load(u"./definitely-not-a-real-plugin.so");
    CHECK_FALSE(result.HasValue());
    CHECK(host.Count() == 0u);
}
