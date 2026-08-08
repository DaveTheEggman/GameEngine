// HelloWindow - the minimal Draconic app: opens a window and runs the frame loop
// until the window is closed. Demonstrates the full path Core -> Runtime
// (Context/Subsystem) -> Shell (SDL3) -> ApplicationHost driving an
// IApplication, wired by DRACONIC_APP_MAIN. Run it directly; close the window to exit.

#include "Core/Prelude.h"
#include "Runtime.Client/AppMain.h"

import draconic.core;
import draconic.runtime;
import draconic.runtime.client;
import draconic.shell;
import draconic.runtime.desktop;
import draconic.shell.desktop;
import draconic.graphics; // GraphicsDevice + FrameContext (DRACONIC_APP_MAIN sets up the device)
import draconic.graphics.gpu; // CreateGraphicsDevice

namespace core = draconic::core;
namespace runtime = draconic::runtime;
namespace graphics = draconic::graphics;
namespace shell = draconic::shell;

namespace
{
    class HelloApp final : public runtime::IApplication
    {
    public:
        void OnStartup(runtime::IApplicationHost&) override
        {
            core::ConsoleWrite(u8"HelloWindow: started - close the window to exit.\n");
        }

        void OnUpdate(runtime::IApplicationHost&, core::f32 deltaTime) override
        {
            m_elapsed += deltaTime;
            ++m_frames;
        }

        void OnRenderWindow(runtime::IApplicationHost&, graphics::FrameContext& frame) override
        {
            frame.Clear(0.10f, 0.10f, 0.12f, 1.0f); // a calm dark grey
        }

        void OnShutdown(runtime::IApplicationHost&) override
        {
            core::ConsoleWrite(u8"HelloWindow: shutting down.\n");
        }

    private:
        core::f32 m_elapsed = 0.0f;
        core::u64 m_frames = 0;
    };
}

DRACONIC_APP_MAIN(HelloApp)
