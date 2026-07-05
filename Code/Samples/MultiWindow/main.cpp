// MultiWindow — the runtime-host smoke test (NOT an RHI sample). It exercises the
// promoted render host end to end: a shared GraphicsDevice, the Application's
// per-window render loop, and runtime window creation. It opens TWO OS windows —
// the main one plus a second opened at runtime via OpenWindow() — and clears each
// to a different color every frame. Close the main window to exit.
//
// The path: CreateShell (SDL3) -> CreateGraphicsDevice (Vulkan) -> Application
// (+ OpenWindow) -> RunApplication. No bespoke swapchain/loop code in the app.

#include "Core/Prelude.h"

import draconic.core;
import draconic.rhi;
import draconic.runtime;
import draconic.runtime.client;
import draconic.shell;
import draconic.runtime.desktop;
import draconic.shell.desktop;
import draconic.runtime.graphics;
import draconic.runtime.graphics.gpu;

namespace rc = draconic::core;
namespace rt = draconic::runtime;
        namespace sh = draconic::shell;
namespace rhi = draconic::rhi;

namespace
{
    class MultiWindowApp final : public rt::IApplication
    {
    public:
        void OnStartup(rt::IApplicationHost& host) override
        {
            // windows[0] (the main window) already has a RenderWindow from Start().
            // Open a second OS window at runtime — the same call a detachable UI
            // panel would make.
            sh::WindowSettings ws;
            ws.title  = u8"Draconic - Detached";
            ws.width  = 480;
            ws.height = 360;
            m_second = host.OpenWindow(ws, rt::RenderWindowDesc{});
            rc::ConsoleWrite(u8"MultiWindow: two windows up - close the main window to exit.\n");
        }

        void OnRenderWindow(rt::IApplicationHost&, rt::FrameContext& frame) override
        {
            // Each window clears to its own color, proving independent per-window
            // presentation through the shared device.
            const rhi::ClearColor color = (frame.window == m_second)
                ? rhi::ClearColor{ 0.85f, 0.45f, 0.20f, 1.0f }   // warm
                : rhi::ClearColor::CornflowerBlue();             // main
            frame.BeginBackbufferPass(color);
            frame.EndBackbufferPass();
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"MultiWindow: shutting down.\n");
        }

    private:
        rt::RenderWindow* m_second = nullptr;
    };
}

int main(int /*argc*/, char** /*argv*/)
{
    sh::WindowSettings ws;
    ws.title  = u8"Draconic - Main";
    ws.width  = 800;
    ws.height = 600;

    auto shell = sh::CreateShell(ws);
    if (shell.Get() == nullptr || shell->MainWindow() == nullptr)
    {
        rc::ConsoleWrite(u8"MultiWindow: shell/window init failed.\n");
        return 1;
    }

    rt::GraphicsDeviceDesc gdd;
    gdd.backend          = rt::BackendType::Vulkan;
    gdd.enableValidation = true;
    auto gpu = rt::CreateGraphicsDevice(gdd);
    if (!gpu.HasValue())
    {
        rc::ConsoleWrite(u8"MultiWindow: graphics device creation failed.\n");
        return 1;
    }

    MultiWindowApp app;
    return rt::RunApplication(app, *shell, gpu.Value().Get());
}
