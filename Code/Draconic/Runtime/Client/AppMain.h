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
//   #include "Runtime/Client/AppMain.h"
//   class MyApp final : public draconic::runtime::IApplication { ... };
//   DRACONIC_APP_MAIN(MyApp)

#ifndef DRACONIC_RUNTIME_CLIENT_APPMAIN_H
#define DRACONIC_RUNTIME_CLIENT_APPMAIN_H

#include <string_view> // the backend-flag scan in the macro body

// The body is the same on all desktop OSes for now. A windowed Win32 build will
// later want wWinMain (no console); main is correct for console/CI builds and is
// a fine starting point. Emscripten/Android targets provide their own entry.
// If GPU device creation fails (no Vulkan), the app still runs windowless-headless.
// The backend flag scan lives INSIDE the macro so it expands at the use site,
// where the app's module imports (draconic.graphics) are in effect - a header-scope
// function would be parsed at include time, before any import.
#define DRACONIC_APP_MAIN(AppType)                                                                 \
    int main(int argc, char** argv)                                                                \
    {                                                                                              \
        auto shell = ::draconic::shell::CreateShell();                                             \
        ::draconic::graphics::GraphicsDeviceDesc draconicGpuDesc{};                                \
        for (int draconicArg = 1; draconicArg < argc; ++draconicArg)                               \
        {                                                                                          \
            const ::std::string_view draconicFlag = argv[draconicArg];                             \
            if (draconicFlag == "--vulkan" || draconicFlag == "--vk")                              \
                draconicGpuDesc.backend = ::draconic::graphics::BackendType::Vulkan;               \
            else if (draconicFlag == "--dx12" || draconicFlag == "--d3d12")                        \
                draconicGpuDesc.backend = ::draconic::graphics::BackendType::DX12;                 \
            else if (draconicFlag == "--webgpu" || draconicFlag == "--wgpu")                       \
                draconicGpuDesc.backend = ::draconic::graphics::BackendType::WebGPU;               \
            else if (draconicFlag == "--null-gpu")                                                 \
                draconicGpuDesc.backend = ::draconic::graphics::BackendType::Null;                 \
        }                                                                                          \
        auto draconicGpu = ::draconic::graphics::CreateGraphicsDevice(draconicGpuDesc);            \
        ::draconic::graphics::GraphicsDevice* draconicDevice =                                     \
            draconicGpu.HasValue() ? draconicGpu.Value().Get() : nullptr;                          \
        AppType app;                                                                               \
        return ::draconic::runtime::RunApplication(app, *shell, draconicDevice);                   \
    }

#endif // DRACONIC_RUNTIME_CLIENT_APPMAIN_H
