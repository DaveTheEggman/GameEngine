// RAPTOR_APP_MAIN(AppType) — generates the program entry point for a client app.
//
// This is a classic header (macros can't live in a module). Use it in the app's
// main translation unit, which must import:
//   - raptor.runtime.client          (IApplication + RunApplication)
//   - raptor.runtime.platform.desktop (CreatePlatform)
//   - raptor.runtime.graphics + raptor.runtime.graphics.gpu (the GPU device)
// The entry point creates the platform window, a GraphicsDevice (so the window
// actually presents — without one the window never becomes visible on Wayland),
// the app, and hands all three to the desktop runner (which drives an
// ApplicationHost). AppType must be an IApplication (or DefaultApplication), and
// should clear/draw in OnRenderWindow (e.g. `frame.Clear(...)`).
//
//   import raptor.runtime.client;
//   import raptor.runtime.platform.desktop;
//   import raptor.runtime.graphics;
//   import raptor.runtime.graphics.gpu;
//   #include "Runtime/Client/AppMain.h"
//   class MyApp final : public raptor::runtime::IApplication { ... };
//   RAPTOR_APP_MAIN(MyApp)

#ifndef RAPTOR_RUNTIME_CLIENT_APPMAIN_H
#define RAPTOR_RUNTIME_CLIENT_APPMAIN_H

// The body is the same on all desktop OSes for now. A windowed Win32 build will
// later want wWinMain (no console); main is correct for console/CI builds and is
// a fine starting point. Emscripten/Android targets provide their own entry.
// If GPU device creation fails (no Vulkan), the app still runs windowless-headless.
#define RAPTOR_APP_MAIN(AppType)                                                      \
    int main(int /*argc*/, char** /*argv*/)                                           \
    {                                                                                 \
        auto platform = ::raptor::runtime::CreatePlatform();                          \
        ::raptor::runtime::GraphicsDeviceDesc raptorGpuDesc{};                        \
        auto raptorGpu = ::raptor::runtime::CreateGraphicsDevice(raptorGpuDesc);      \
        ::raptor::runtime::GraphicsDevice* raptorDevice =                             \
            raptorGpu.HasValue() ? raptorGpu.Value().Get() : nullptr;                 \
        AppType app;                                                                  \
        return ::raptor::runtime::RunApplication(app, *platform, raptorDevice);       \
    }

#endif // RAPTOR_RUNTIME_CLIENT_APPMAIN_H
