// DRACONIC_APP_MAIN(AppType) - generates the program entry point for a client app.
//
// This is a classic header (macros can't live in a module). Use it in the app's
// main translation unit, which must import:
//   - draconic.runtime.client          (IApplication)
//   - draconic.runtime.desktop         (RunApplication - the desktop runner)
//   - draconic.shell.desktop           (CreateShell)
//   - draconic.graphics + draconic.graphics.gpu (the GPU device)
// The entry point creates the shell window, a GraphicsDevice (so the window
// actually presents - without one the window never becomes visible on Wayland),
// the app, and hands all three to the desktop runner (which drives an
// ApplicationHost). AppType must be an IApplication (or DefaultApplication), and
// should clear/draw in OnRenderWindow (e.g. `frame.Clear(...)`).
//
//   import draconic.runtime.client;
//   import draconic.runtime.desktop;
//   import draconic.shell.desktop;
//   import draconic.graphics;
//   import draconic.graphics.gpu;
//   #include "Runtime.Client/AppMain.h"
//   class MyApp final : public foundation::runtime::IApplication { ... };
//   DRACONIC_APP_MAIN(MyApp)

#ifndef DRACONIC_RUNTIME_CLIENT_APPMAIN_H
#define DRACONIC_RUNTIME_CLIENT_APPMAIN_H

#include "Core/Prelude.h" // DRACONIC_PLATFORM_WEB (picks the desktop vs browser entry body)
#include "Core/Log/Log.h"  // the build-stamp startup line

// The build identity compiled into Runtime.Client (GenerateBuildStamp.cmake): git short
// hash + dirty flag + build minute. Logged first thing by DRACONIC_APP_MAIN so a running
// binary - ESPECIALLY a browser-cached .wasm - can always be matched to a build.
extern "C" const char* DraconicBuildStamp();

#if DRACONIC_PLATFORM_WEB

// Emscripten entry. Unlike desktop, the web runner (draconic.runtime.web) does NOT block: it
// registers a requestAnimationFrame callback and returns, and the browser drives frames after
// main() unwinds. So the shell, the GPU device, and the app must OUTLIVE main's return - they live
// in static storage here. The backend is always WebGPU (there is no argv), and device creation
// blocks-and-yields to the browser via ASYNCIFY (see the WebGPU backend's pump), so the link must
// enable ASYNCIFY. The app TU imports draconic.shell.web + draconic.runtime.web (not the desktop
// pair). The canvas defaults to "#canvas" (Emscripten's default shell canvas id).
#define DRACONIC_APP_MAIN(AppType)                                                                 \
    int main()                                                                                     \
    {                                                                                              \
        static ::foundation::core::ConsoleSink draconicConsoleSink;                                  \
        ::foundation::core::GlobalLogger().AddSink(&draconicConsoleSink);                            \
        ::foundation::core::GlobalLogger().SetMinLevel(::foundation::core::LogLevel::Info);            \
        DRACONIC_LOG_INFO(u8"Build", u8"Draconic build {}",                                        \
                          reinterpret_cast<const char8_t*>(DraconicBuildStamp()));                 \
        static ::foundation::shell::WebShell draconicShell;                                          \
        ::foundation::graphics::GraphicsDeviceDesc draconicGpuDesc{};                                \
        draconicGpuDesc.backend = ::foundation::graphics::BackendType::WebGPU;                       \
        static auto draconicGpu = ::foundation::graphics::CreateGraphicsDevice(draconicGpuDesc);     \
        ::foundation::graphics::GraphicsDevice* draconicDevice =                                     \
            draconicGpu.HasValue() ? draconicGpu.Value().Get() : nullptr;                          \
        static AppType draconicApp;                                                                \
        return ::foundation::runtime::RunApplication(draconicApp, draconicShell, draconicDevice);    \
    }

#else

// The body is the same on all desktop OSes for now. A windowed Win32 build will
// later want wWinMain (no console); main is correct for console/CI builds and is
// a fine starting point. Android targets provide their own entry.
// If GPU device creation fails (no Vulkan), the app still runs windowless-headless.
#define DRACONIC_APP_MAIN(AppType)                                                                 \
    int main(int argc, char** argv)                                                                \
    {                                                                                              \
        static ::foundation::core::ConsoleSink draconicConsoleSink;                                  \
        ::foundation::core::GlobalLogger().AddSink(&draconicConsoleSink);                            \
        DRACONIC_LOG_INFO(u8"Build", u8"Draconic build {}",                                        \
                          reinterpret_cast<const char8_t*>(DraconicBuildStamp()));                 \
        auto shell = ::foundation::shell::CreateShell();                                             \
        ::foundation::graphics::GraphicsDeviceDesc draconicGpuDesc{};                                \
        draconicGpuDesc.backend =                                                                  \
            ::foundation::graphics::SelectBackendFromArguments(argc, argv);                          \
        auto draconicGpu = ::foundation::graphics::CreateGraphicsDevice(draconicGpuDesc);            \
        ::foundation::graphics::GraphicsDevice* draconicDevice =                                     \
            draconicGpu.HasValue() ? draconicGpu.Value().Get() : nullptr;                          \
        AppType app;                                                                               \
        return ::foundation::runtime::RunApplication(app, *shell, draconicDevice);                   \
    }

#endif // DRACONIC_PLATFORM_WEB

#endif // DRACONIC_RUNTIME_CLIENT_APPMAIN_H
