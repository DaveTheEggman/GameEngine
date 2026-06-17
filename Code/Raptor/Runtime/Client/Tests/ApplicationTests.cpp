#include <doctest/doctest.h>

#include "Core/Prelude.h"  // <new> reachability for container instantiation (GCC)

import raptor.core;
import raptor.runtime;
import raptor.runtime.platform;
import raptor.runtime.client;

using namespace raptor::core;
using namespace raptor::runtime;

namespace
{
    // A minimal in-process platform: counts ProcessEvents, and can be made to
    // quit (as if the window closed) to exercise the runner's exit conditions.
    class MockPlatform final : public IPlatform
    {
    public:
        int processed = 0;
        bool running = true;
        IWindow* MainWindow() noexcept override { return nullptr; }
        IInputManager* Input() noexcept override { return nullptr; }
        void ProcessEvents() override { ++processed; }
        bool IsRunning() const noexcept override { return running; }
        void RequestExit() override { running = false; }
    };

    // Counts the frame phases the Application drives into the Context.
    class CountingSys final : public Subsystem
    {
    public:
        int begin = 0, fixed = 0, update = 0, post = 0, end = 0, inits = 0, shutdowns = 0;
        void BeginFrame(f32) override { ++begin; }
        void FixedUpdate(f32) override { ++fixed; }
        void Update(f32) override { ++update; }
        void PostUpdate(f32) override { ++post; }
        void EndFrame() override { ++end; }
    protected:
        void OnInit() override { ++inits; }
        void OnShutdown() override { ++shutdowns; }
    };

    // Records the lifecycle hook order: 1=configure 2=initialize 3=started
    // 4=update 5=shutdown. Uses a 0.5s fixed step so accumulator math is exact.
    class LifecycleApp final : public Application
    {
    public:
        Array<int> order;
        CountingSys* sys = nullptr;
        bool sysLiveAtStarted = false;
    protected:
        void OnConfigure(ApplicationSettings& s) override { order.PushBack(1); s.fixedTimeStep = 0.5f; }
        void OnInitialize() override { order.PushBack(2); sys = Ctx().AddSubsystem<CountingSys>(); }
        void OnStarted() override { order.PushBack(3); sysLiveAtStarted = sys->IsInitialized(); }
        void OnUpdate(f32) override { order.PushBack(4); }
        void OnShutdown() override { order.PushBack(5); }
    };
}

TEST_CASE("client: Start runs the lifecycle hooks in order and brings subsystems up")
{
    LifecycleApp app;
    app.Start();

    REQUIRE(app.order.Size() == 3u);
    CHECK(app.order[0] == 1);          // OnConfigure
    CHECK(app.order[1] == 2);          // OnInitialize
    CHECK(app.order[2] == 3);          // OnStarted
    CHECK(app.sysLiveAtStarted);       // subsystem Init ran before OnStarted
    CHECK(app.IsRunning());

    app.Stop();
    CHECK_FALSE(app.IsRunning());
    CHECK(app.sys->shutdowns == 1);
    CHECK(app.order[app.order.Size() - 1] == 5);  // OnShutdown last
}

TEST_CASE("client: Tick drives Context phases with a fixed-step accumulator")
{
    LifecycleApp app;
    app.Start();

    app.Tick(0.25f);                   // accumulator 0.25 < 0.5 -> no fixed step
    CHECK(app.sys->begin == 1);
    CHECK(app.sys->update == 1);
    CHECK(app.sys->post == 1);
    CHECK(app.sys->end == 1);
    CHECK(app.sys->fixed == 0);

    app.Tick(0.25f);                   // accumulator reaches 0.5 -> exactly one fixed step
    CHECK(app.sys->fixed == 1);
    CHECK(app.sys->update == 2);

    app.Tick(0.5f);                    // another full step
    CHECK(app.sys->fixed == 2);

    // OnUpdate fired once per Tick, after Context::Update each time.
    int updates = 0;
    for (usize i = 0; i < app.order.Size(); ++i) { if (app.order[i] == 4) { ++updates; } }
    CHECK(updates == 3);

    app.Stop();
}

TEST_CASE("client: maxFrameTime clamps a large delta")
{
    struct ClampApp final : Application
    {
        CountingSys* sys = nullptr;
    protected:
        void OnConfigure(ApplicationSettings& s) override { s.fixedTimeStep = 0.1f; s.maxFrameTime = 0.25f; }
        void OnInitialize() override { sys = Ctx().AddSubsystem<CountingSys>(); }
    } app;
    app.Start();

    // A 10s spike must be clamped by the caller; the runner is responsible for
    // clamping to Settings().maxFrameTime before calling Tick.
    f32 dt = 10.0f;
    if (dt > app.Settings().maxFrameTime) { dt = app.Settings().maxFrameTime; }
    app.Tick(dt);                      // 0.25 / 0.1 -> 2 fixed steps, not 100
    CHECK(app.sys->fixed == 2);

    app.Stop();
}

TEST_CASE("client: RequestExit stops a manual run loop")
{
    LifecycleApp app;
    app.Start();

    // Emulate a platform runner: tick while running, exit after 3 updates.
    int frames = 0;
    while (app.IsRunning())
    {
        app.Tick(0.5f);
        if (++frames == 3) { app.RequestExit(7); }
    }
    app.Stop();

    CHECK(frames == 3);
    CHECK(app.ExitCode() == 7);
    CHECK(app.sys->update == 3);
}

namespace
{
    // App that records the borrowed platform it sees during OnInitialize.
    class PlatformApp final : public Application
    {
    public:
        IPlatform* seenPlatform = nullptr;
    protected:
        void OnInitialize() override { seenPlatform = Platform(); }  // platform available at init
    };
}

TEST_CASE("client: Start borrows the platform and exposes it from OnInitialize")
{
    MockPlatform platform;
    PlatformApp app;

    CHECK(app.Platform() == nullptr);    // none until Start
    app.Start(&platform);
    CHECK(app.seenPlatform == &platform);  // visible during OnInitialize
    CHECK(app.Platform() == &platform);
    app.Stop();

    // The runner (RunApplication) that drives ProcessEvents + Tick lives in the
    // platform backend, not here; it is exercised by the desktop backend tests.
}
