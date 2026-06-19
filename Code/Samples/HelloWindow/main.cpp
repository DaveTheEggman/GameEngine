// HelloWindow — the minimal Raptor client app: opens a window and runs the
// frame loop until the window is closed. Demonstrates the full path
// Core -> Runtime (Context/Subsystem) -> Platform (SDL3) -> Application, wired
// by RAPTOR_APP_MAIN. Run it directly; close the window to exit.

#include "Core/Prelude.h"
#include "Runtime/Client/AppMain.h"

import raptor.core;
import raptor.runtime;
import raptor.runtime.client;
import raptor.runtime.platform;
import raptor.runtime.platform.desktop;

namespace rc = raptor::core;

namespace
{
    class HelloApp final : public raptor::runtime::Application
    {
    protected:
        void OnStarted() override
        {
            rc::ConsoleWrite(u8"HelloWindow: started - close the window to exit.\n");
        }

        void OnUpdate(rc::f32 deltaTime) override
        {
            m_elapsed += deltaTime;
            ++m_frames;
        }

        void OnShutdown() override
        {
            rc::ConsoleWrite(u8"HelloWindow: shutting down.\n");
        }

    private:
        rc::f32 m_elapsed = 0.0f;
        rc::u64 m_frames = 0;
    };
}

RAPTOR_APP_MAIN(HelloApp)
