// MultiWindow — the runtime-host smoke test (NOT an RHI sample). It exercises the
// promoted render host end to end: a shared GraphicsDevice, the Application's
// per-window render loop, and runtime window creation. It opens TWO OS windows —
// the main one plus a second opened at runtime via OpenWindow() — and clears each
// to a different color every frame. Close the main window to exit.
//
// The path: CreatePlatform (SDL3) -> CreateGraphicsDevice (Vulkan) -> Application
// (+ OpenWindow) -> RunApplication. No bespoke swapchain/loop code in the app.

#include "Core/Prelude.h"

import raptor.core;
import raptor.rhi;
import raptor.runtime;
import raptor.runtime.client;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;
import raptor.runtime.graphics;
import raptor.runtime.graphics.gpu;

namespace rc = raptor::core;
namespace rt = raptor::runtime;
namespace rhi = raptor::rhi;

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
            rt::WindowSettings ws;
            ws.title  = u8"Raptor - Detached";
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
    rt::WindowSettings ws;
    ws.title  = u8"Raptor - Main";
    ws.width  = 800;
    ws.height = 600;

    auto platform = rt::CreatePlatform(ws);
    if (platform.Get() == nullptr || platform->MainWindow() == nullptr)
    {
        rc::ConsoleWrite(u8"MultiWindow: platform/window init failed.\n");
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
    return rt::RunApplication(app, *platform, gpu.Value().Get());
}
