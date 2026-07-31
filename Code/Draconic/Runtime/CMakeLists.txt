# Draconic::Runtime - the `draconic.runtime` module (Context + Subsystems).

add_library(DraconicRuntime STATIC)
add_library(Draconic::Runtime ALIAS DraconicRuntime)

target_link_libraries(DraconicRuntime PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicRuntime
    PUBLIC
        FILE_SET runtime_modules TYPE CXX_MODULES FILES
            RuntimeModule.cppm
            Subsystem.cppm
            RuntimeContext.cppm
            Plugin.cppm
            PluginHost.cppm)

# The client Application loop (draconic.runtime.client) is loop-agnostic and its deps (Runtime,
# Shell, Graphics, Profiler) are all up on web, so it climbs on every platform. The RUNNER that
# drives it is execution-model-specific: Desktop (blocking wall-clock loop) on native, Web
# (emscripten_set_main_loop, yielding to the browser) on Emscripten.
add_subdirectory(Client)
# The game-instance/scripting composition + the opinionated DefaultApplication. Their subsystem +
# resource deps all climb for web now, and AngelScript (their only web-unsafe dep) is gated inside
# DefaultApp, so they build on every platform. DefaultApplication is what a full app on web uses.
add_subdirectory(GameInstance)   # a running game as a first-class object (DefaultApp owns one)
add_subdirectory(DefaultApp)

if(EMSCRIPTEN)
    add_subdirectory(Web) # draconic.runtime.web: the browser run loop (sibling of Desktop)
else()

    # Desktop runner (draconic.runtime.desktop): the blocking desktop run loop that drives an
    # IApplication (client) against an IShell (shell) with a GraphicsDevice - depends on all three.
    add_subdirectory(Desktop)

    if(DRACONIC_BUILD_TESTS)
        add_subdirectory(Tests)
    endif()
endif()
