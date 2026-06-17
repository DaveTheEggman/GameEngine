// Raptor::RuntimeClient — the `raptor.runtime.client` module.
//
// Application: the headless client. An abstract base class that owns a Context
// and exposes the frame lifecycle — Start (configure -> register subsystems ->
// Context startup -> ready), per-frame Tick (fixed-step accumulator + variable
// update), and Stop. It is deliberately LOOP-AGNOSTIC: who owns the run loop is
// platform-specific (a blocking while-loop on desktop, a browser callback on
// Emscripten), so the platform layer drives Start/Tick/Stop — the Application
// does not contain a loop. The base registers no subsystems and does no
// rendering; it stays headless until Platform/RHI land (OnRender is a no-op for
// now). Real logic lives in subsystems and the script driver; the On* hooks are
// thin convenience sugar.

module;
#include "Core/Prelude.h"

export module raptor.runtime.client;

import raptor.core;
import raptor.runtime;
import raptor.runtime.platform;

namespace rc = raptor::core;

export namespace raptor::runtime
{
    struct ApplicationSettings
    {
        rc::f32 fixedTimeStep = 1.0f / 60.0f; // seconds per fixed update
        rc::f32 maxFrameTime  = 0.25f;        // clamp per frame (avoids the spiral of death)
    };

    class Application
    {
    public:
        Application() = default;
        virtual ~Application() = default;

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        // Bring the application up: configure, register subsystems, start the
        // Context, then signal ready. Idempotent. The platform runner calls this
        // once (passing the platform), then Tick() each frame while IsRunning(),
        // then Stop(). The platform is borrowed (the entry point owns it) and is
        // available from OnInitialize on, so platform-backed subsystems can be
        // wired there; it stays null for headless runs.
        void Start(IPlatform* platform = nullptr)
        {
            if (m_started) { return; }
            m_platform = platform;
            OnConfigure(m_settings);
            OnInitialize();
            m_context.Startup();
            OnStarted();
            m_started = true;
            m_running = true;
        }

        // Advance exactly one frame with an explicit delta. The platform runner
        // passes wall-clock time; call directly for deterministic stepping.
        void Tick(rc::f32 deltaTime)
        {
            m_context.BeginFrame(deltaTime);

            m_accumulator += deltaTime;
            while (m_accumulator >= m_settings.fixedTimeStep)
            {
                m_context.FixedUpdate(m_settings.fixedTimeStep);
                m_accumulator -= m_settings.fixedTimeStep;
            }

            m_context.Update(deltaTime);
            OnUpdate(deltaTime);
            m_context.PostUpdate(deltaTime);
            OnRender();
            m_context.EndFrame();
        }

        // Tear the application down: stop the Context, then notify. Idempotent.
        void Stop()
        {
            if (!m_started) { return; }
            m_context.Shutdown();
            OnShutdown();
            m_started = false;
            m_running = false;
        }

        // Ask the platform loop to stop after the current frame (it polls
        // IsRunning()). Does not itself tear down — the runner calls Stop() once
        // the loop exits, and returns ExitCode() from the entry point.
        void RequestExit(int code = 0) noexcept { m_running = false; m_exitCode = code; }

        [[nodiscard]] Context& Ctx() noexcept { return m_context; }
        // Borrowed platform service (null for headless runs). Use from
        // OnInitialize on to wire platform-backed subsystems.
        [[nodiscard]] IPlatform* Platform() noexcept { return m_platform; }
        [[nodiscard]] const ApplicationSettings& Settings() const noexcept { return m_settings; }
        [[nodiscard]] bool IsRunning() const noexcept { return m_running; }
        [[nodiscard]] int ExitCode() const noexcept { return m_exitCode; }

    protected:
        virtual void OnConfigure(ApplicationSettings& /*settings*/) {}  // tweak settings, pre-init
        virtual void OnInitialize() {}                                  // register subsystems (before Startup)
        virtual void OnStarted() {}                                     // subsystems live (load scripts, set driver)
        virtual void OnUpdate(rc::f32 /*deltaTime*/) {}                 // after Context::Update
        virtual void OnRender() {}                                      // headless: no-op for now
        virtual void OnShutdown() {}

    private:
        Context m_context;
        ApplicationSettings m_settings;
        IPlatform* m_platform = nullptr;  // borrowed; owned by the entry point
        bool m_started = false;
        bool m_running = false;
        int m_exitCode = 0;
        rc::f32 m_accumulator = 0.0f;
    };
}
