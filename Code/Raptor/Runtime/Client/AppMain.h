// RAPTOR_APP_MAIN(AppType) — generates the program entry point for a client app.
//
// This is a classic header (macros can't live in a module). Use it in the app's
// main translation unit, which must import raptor.runtime.client (Application +
// RunApplication) and a platform backend that provides CreatePlatform() (e.g.
// raptor.runtime.platform.desktop). The entry point creates the platform, the
// app, and hands both to the desktop runner.
//
//   import raptor.runtime.client;
//   import raptor.runtime.platform.desktop;
//   #include "Runtime/Client/AppMain.h"
//   class MyApp final : public raptor::runtime::Application { ... };
//   RAPTOR_APP_MAIN(MyApp)

#ifndef RAPTOR_RUNTIME_CLIENT_APPMAIN_H
#define RAPTOR_RUNTIME_CLIENT_APPMAIN_H

// The body is the same on all desktop OSes for now. A windowed Win32 build will
// later want wWinMain (no console); main is correct for console/CI builds and is
// a fine starting point. Emscripten/Android targets provide their own entry.
#define RAPTOR_APP_MAIN(AppType)                                       \
    int main(int /*argc*/, char** /*argv*/)                            \
    {                                                                  \
        auto platform = ::raptor::runtime::CreatePlatform();           \
        AppType app;                                                   \
        return ::raptor::runtime::RunApplication(app, *platform);      \
    }

#endif // RAPTOR_RUNTIME_CLIENT_APPMAIN_H
