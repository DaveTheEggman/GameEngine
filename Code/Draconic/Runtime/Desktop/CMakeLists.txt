# Draconic::RuntimeDesktop - `draconic.runtime.desktop` (the desktop application runner:
# a blocking wall-clock loop driving an IApplication against an IShell). Works through the
# abstract IShell interface, so it needs the shell interface (not a backend) plus the client
# (IApplication/ApplicationHost) and graphics (GraphicsDevice). The web/emscripten runner will
# be its sibling.

add_library(DraconicRuntimeDesktop STATIC)
add_library(Draconic::RuntimeDesktop ALIAS DraconicRuntimeDesktop)

target_link_libraries(DraconicRuntimeDesktop
    PUBLIC Draconic::Shell Draconic::RuntimeClient Draconic::Graphics Draconic::Policy)

target_sources(DraconicRuntimeDesktop
    PUBLIC
        FILE_SET runtime_desktop_modules TYPE CXX_MODULES FILES
            DesktopRunner.cppm)
