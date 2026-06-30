// Draconic::RuntimeClient — the `draconic.runtime.client` module.
//
// ApplicationHost: the concrete, generic host that drives exactly ONE IApplication.
// Owns a Context, an optional (borrowed) GraphicsDevice, and the LIST of
// RenderWindows it presents. It is infrastructure, NOT subclassed — all behavior
// lives in the IApplication. It is deliberately LOOP-AGNOSTIC: the platform layer
// drives Start/Tick/Stop (a blocking loop on desktop, a callback on Emscripten),
// so the host contains no run loop.
//
// The host implements IApplicationHost (the view the application gets of it). The
// application registers its subsystems in Configure() — the host forces in none —
// so the subsystem set is the application's alone and identical standalone or in
// the editor.
//
// Multi-window is uniform: the main window is windows[0]; every frame renders the
// whole list. Windows can be opened/closed at runtime (OpenWindow/CloseWindow) —
// the basis for detachable UI windows — with close deferred to frame end.

module;
#include "Core/Prelude.h"
#include "Profiler/Profiler.h"

export module draconic.runtime.client;

export import :app;   // ApplicationSettings, IApplicationHost, IApplication

import draconic.core;
import draconic.runtime;
import draconic.runtime.platform;
import draconic.runtime.graphics;
import draconic.profiler;

namespace rc = draconic::core;

export namespace draconic::runtime
{
    class ApplicationHost final : public IApplicationHost
    {
    public:
        ApplicationHost() = default;
        ~ApplicationHost() override = default;

        ApplicationHost(const ApplicationHost&) = delete;
        ApplicationHost& operator=(const ApplicationHost&) = delete;

        // Bring the application up: read settings, register subsystems (app's
        // Configure), start the Context, create the main RenderWindow (if platform
        // +graphics), then enter play (app OnLaunch). Idempotent. The application,
        // platform, and graphics device are all BORROWED (owned by the entry point);
        // platform/graphics stay null for headless runs.
        void Start(IApplication& app, IPlatform* platform = nullptr, GraphicsDevice* graphics = nullptr)
        {
            if (m_started) { return; }
            m_app = &app;
            m_platform = platform;
            m_graphics = graphics;
            m_settings = app.Settings();

            // Bring up the engine-wide JobSystem before any subsystem starts, so it is
            // available to all of them and outlives them (torn down last, in Stop()).
            rc::InitGlobalJobSystem();

            m_app->Configure(*this);
            m_context.Startup();

            // The main window already exists on the platform; give it a RenderWindow.
            if (m_platform != nullptr && m_graphics != nullptr && m_platform->WindowManager() != nullptr)
            {
                if (IWindow* main = m_platform->WindowManager()->MainWindow())
                {
                    RenderWindowDesc mainDesc{};
                    mainDesc.presentMode = m_settings.presentMode;   // honor the app's vsync choice
                    auto rw = m_graphics->CreateRenderWindow(*main, mainDesc);
                    if (rw.HasValue()) { m_windows.PushBack(static_cast<rc::UniquePtr<RenderWindow>&&>(rw.Value())); }
                }
            }

            m_app->OnStartup(*this);
            m_app->OnLaunch(*this);   // standalone enters play immediately

            m_started = true;
            m_running = true;
        }

        // Advance exactly one frame with an explicit delta. The platform runner
        // passes wall-clock time; call directly for deterministic stepping.
        void Tick(rc::f32 deltaTime)
        {
            DRACONIC_PROFILE_FRAME_BEGIN();
            m_context.BeginFrame(deltaTime);

            {
                DRACONIC_PROFILE_SCOPE("Update");
                m_accumulator += deltaTime;
                while (m_accumulator >= m_settings.fixedTimeStep)
                {
                    m_context.FixedUpdate(m_settings.fixedTimeStep);
                    m_app->OnFixedUpdate(*this, m_settings.fixedTimeStep);
                    m_accumulator -= m_settings.fixedTimeStep;
                }

                m_context.Update(deltaTime);
                m_app->OnUpdate(*this, deltaTime);
                m_context.PostUpdate(deltaTime);
            }

            // Render every window uniformly (main == windows[0]).
            if (m_graphics != nullptr)
            {
                DRACONIC_PROFILE_SCOPE("Render");
                for (auto& rw : m_windows)
                {
                    rw->SyncSize();
                    // Acquire BLOCKS the CPU until a swapchain image is free — under vsync (or a
                    // GPU-bound frame) this is where the CPU waits for the display/GPU, so scope it
                    // separately to tell a healthy present-wait from a real stall.
                    FrameContext frame{};
                    {
                        DRACONIC_PROFILE_SCOPE("Render.Acquire");
                        frame = rw->BeginFrame();
                    }
                    if (!frame.valid) { continue; }
                    m_app->OnRenderWindow(*this, frame);
                    {
                        DRACONIC_PROFILE_SCOPE("Render.Present");   // record submit + queue present
                        rw->EndFrame(frame);
                    }
                }
                {
                    DRACONIC_PROFILE_SCOPE("Render.Advance");   // ring step; may wait on the frame fence
                    m_graphics->AdvanceFrame();
                }
            }

            m_context.EndFrame();
            FlushPendingCloses();
            DRACONIC_PROFILE_FRAME_END();
        }

        // Tear the application down: leave play, stop the Context, destroy windows.
        // Idempotent.
        void Stop()
        {
            if (!m_started) { return; }
            m_app->OnExit(*this);
            m_context.Shutdown();
            m_app->OnShutdown(*this);

            m_pendingClose.Clear();
            m_windows.Clear();  // RenderWindow dtors WaitIdle + free GPU resources

            // Tear down the engine-wide JobSystem last — after every subsystem (Context.Shutdown)
            // and all GPU resource frees (window dtors), so nothing references it afterward.
            rc::ShutdownGlobalJobSystem();

            m_started = false;
            m_running = false;
        }

        // --- IApplicationHost ---
        void RequestExit(int code = 0) noexcept override { m_running = false; m_exitCode = code; }

        [[nodiscard]] Context& Ctx() noexcept override { return m_context; }
        [[nodiscard]] IPlatform* Platform() noexcept override { return m_platform; }
        [[nodiscard]] GraphicsDevice* Graphics() noexcept override { return m_graphics; }

        RenderWindow* OpenWindow(const WindowSettings& windowSettings, const RenderWindowDesc& renderDesc) override
        {
            if (m_platform == nullptr || m_graphics == nullptr) { return nullptr; }
            IWindowManager* wm = m_platform->WindowManager();
            if (wm == nullptr) { return nullptr; }

            auto osWindow = wm->CreateWindow(windowSettings);
            if (!osWindow.HasValue()) { return nullptr; }

            auto rw = m_graphics->CreateRenderWindow(*osWindow.Value(), renderDesc);
            if (!rw.HasValue())
            {
                wm->DestroyWindow(osWindow.Value());
                wm->FlushDestroyed();
                return nullptr;
            }

            RenderWindow* ptr = rw.Value().Get();
            m_windows.PushBack(static_cast<rc::UniquePtr<RenderWindow>&&>(rw.Value()));
            return ptr;
        }

        void CloseWindow(RenderWindow* window) override
        {
            if (window == nullptr) { return; }
            for (RenderWindow* p : m_pendingClose) { if (p == window) { return; } }  // already queued
            m_pendingClose.PushBack(window);
        }

        [[nodiscard]] const ApplicationSettings& Settings() const noexcept { return m_settings; }
        [[nodiscard]] bool IsRunning() const noexcept { return m_running; }
        [[nodiscard]] int ExitCode() const noexcept { return m_exitCode; }
        [[nodiscard]] rc::Span<const rc::UniquePtr<RenderWindow>> Windows() const noexcept
        {
            return rc::Span<const rc::UniquePtr<RenderWindow>>(m_windows.Data(), m_windows.Size());
        }

    private:
        // Destroy windows queued by CloseWindow: free the RenderWindow (GPU) then
        // the OS window. Runs at frame end, after the GPU finished the frame.
        void FlushPendingCloses()
        {
            if (m_pendingClose.IsEmpty()) { return; }
            IWindowManager* wm = (m_platform != nullptr) ? m_platform->WindowManager() : nullptr;

            for (RenderWindow* dead : m_pendingClose)
            {
                IWindow* osWindow = &dead->Window();
                for (rc::usize i = 0; i < m_windows.Size(); ++i)
                {
                    if (m_windows[i].Get() == dead) { m_windows.RemoveAt(i); break; }  // dtor frees GPU resources
                }
                if (wm != nullptr) { wm->DestroyWindow(osWindow); }
            }
            m_pendingClose.Clear();
            if (wm != nullptr) { wm->FlushDestroyed(); }
        }

        Context m_context;
        ApplicationSettings m_settings;
        IApplication* m_app = nullptr;         // borrowed; owned by the entry point
        IPlatform* m_platform = nullptr;       // borrowed; owned by the entry point
        GraphicsDevice* m_graphics = nullptr;  // borrowed; owned by the entry point
        rc::Array<rc::UniquePtr<RenderWindow>> m_windows;   // [0] == main
        rc::Array<RenderWindow*> m_pendingClose;            // deferred destroy
        bool m_started = false;
        bool m_running = false;
        int m_exitCode = 0;
        rc::f32 m_accumulator = 0.0f;
    };
}
