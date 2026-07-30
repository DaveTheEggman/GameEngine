# Draconic::RuntimeWeb - `draconic.runtime.web` (the browser application runner: a per-frame
# emscripten_set_main_loop callback driving an IApplication against an IShell, yielding to the
# browser between frames). The Emscripten sibling of draconic.runtime.desktop - same interface
# dependencies (Shell interface + client + graphics), different execution model. Emscripten-only.
add_library(DraconicRuntimeWeb STATIC)
add_library(Draconic::RuntimeWeb ALIAS DraconicRuntimeWeb)

target_link_libraries(DraconicRuntimeWeb
    PUBLIC Draconic::Shell Draconic::RuntimeClient Draconic::Graphics Draconic::Policy)

target_sources(DraconicRuntimeWeb
    PUBLIC
        FILE_SET runtime_web_modules TYPE CXX_MODULES FILES
            WebRunner.cppm)
