// draconic.runtime.desktop - the desktop application runner.
//
// RunApplication is the DESKTOP execution model: a blocking wall-clock loop that drives an
// IApplication (via ApplicationHost) against an IShell until either stops. It lives here -
// separate from the shell backend (draconic.shell.desktop) and from the execution-model-agnostic
// client (draconic.runtime.client) - precisely BECAUSE the loop is execution-model-specific:
// desktop blocks in a while-loop, whereas Emscripten must yield to the browser via a callback
// (its runner will be this module's sibling). The loop works entirely through the abstract IShell
// interface, so it is windowing-backend agnostic; the concrete shell is constructed by the entry
// point and passed in.
module;
#include "Core/Prelude.h"

export module draconic.runtime.desktop;

import draconic.core;
import draconic.shell;          // IShell (interface only - the concrete shell is handed in)
import draconic.graphics;       // GraphicsDevice (handed to the app)
import draconic.runtime.client; // IApplication + ApplicationHost (the runner drives these)

namespace core = draconic::core;
using namespace draconic::graphics; // GraphicsDevice (moved from draconic::runtime)

export namespace draconic::runtime
{
    // Desktop runner: block-loop the app against the shell until either stops, clamped to
    // maxFrameTime. DRACONIC_APP_MAIN calls it on desktop; returns the app's exit code.
    inline int RunApplication(IApplication& app, shell::IShell& shell,
                              GraphicsDevice* graphics = nullptr)
    {
        ApplicationHost host;
        host.Start(app, &shell, graphics);
        core::TimePoint previous = core::Clock::Now();
        while (shell.IsRunning() && host.IsRunning())
        {
            shell.ProcessEvents();
            const core::TimePoint now = core::Clock::Now();
            core::f32 dt = (now - previous).AsSecondsF();
            previous = now;
            if (dt > host.Settings().maxFrameTime)
            {
                dt = host.Settings().maxFrameTime;
            }
            host.Tick(dt);
        }
        host.Stop();
        return host.ExitCode();
    }
}
