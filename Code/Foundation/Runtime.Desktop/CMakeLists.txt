# Foundation::Runtime.Desktop - `foundation.runtime.desktop` (the desktop application runner:
# a blocking wall-clock loop driving an IApplication against an IShell). Works through the
# abstract IShell interface, so it needs the shell interface (not a backend) plus the client
# (IApplication/ApplicationHost) and graphics (GraphicsDevice). The web/emscripten runner will
# be its sibling.

add_library(Runtime.Desktop STATIC)
add_library(Foundation::Runtime.Desktop ALIAS Runtime.Desktop)

target_link_libraries(Runtime.Desktop
    PUBLIC Foundation::Shell Foundation::Runtime.Client Foundation::Graphics Foundation::Policy)

target_sources(Runtime.Desktop
    PUBLIC
        FILE_SET runtime_desktop_modules TYPE CXX_MODULES FILES
            DesktopRunner.cppm)
