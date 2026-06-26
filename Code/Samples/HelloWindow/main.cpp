// HelloWindow — the minimal Raptor app: opens a window and runs the frame loop
// until the window is closed. Demonstrates the full path Core -> Runtime
// (Context/Subsystem) -> Platform (SDL3) -> ApplicationHost driving an
// IApplication, wired by RAPTOR_APP_MAIN. Run it directly; close the window to exit.

#include "Core/Prelude.h"
#include "Runtime/Client/AppMain.h"

import raptor.core;
import raptor.runtime;
import raptor.runtime.client;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;
import raptor.runtime.graphics;       // GraphicsDevice + FrameContext (RAPTOR_APP_MAIN sets up the device)
import raptor.runtime.graphics.gpu;   // CreateGraphicsDevice

namespace rc = raptor::core;
namespace rt = raptor::runtime;

namespace
{
    class HelloApp final : public rt::IApplication
    {
    public:
        void OnStartup(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"HelloWindow: started - close the window to exit.\n");
        }

        void OnUpdate(rt::IApplicationHost&, rc::f32 deltaTime) override
        {
            m_elapsed += deltaTime;
            ++m_frames;
        }

        void OnRenderWindow(rt::IApplicationHost&, rt::FrameContext& frame) override
        {
            frame.Clear(0.10f, 0.10f, 0.12f, 1.0f);   // a calm dark grey
        }

        void OnShutdown(rt::IApplicationHost&) override
        {
            rc::ConsoleWrite(u8"HelloWindow: shutting down.\n");
        }

    private:
        rc::f32 m_elapsed = 0.0f;
        rc::u64 m_frames = 0;
    };
}

RAPTOR_APP_MAIN(HelloApp)
