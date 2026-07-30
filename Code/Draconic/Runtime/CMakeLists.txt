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

# The app/runner sub-libs above the base draconic.runtime lib: the client Application loop, the
# game-instance/scripting composition, and the desktop runner. They link Shell, the script VMs, and
# the scene subsystems - i.e. the app layer - none of which is on web yet, so they are gated off for
# Emscripten (the base runtime lib above still climbs). NOT EMSCRIPTEN rather than TARGET guards
# because the desktop add-order interleaves these deps; the guard is transparent on desktop. A web
# runner will live alongside Desktop once the app backends land.
if(NOT EMSCRIPTEN)
    add_subdirectory(Client)
    add_subdirectory(GameInstance)   # a running game as a first-class object (DefaultApp owns one)
    add_subdirectory(DefaultApp)
    add_subdirectory(Script)

    # Desktop runner (draconic.runtime.desktop): the blocking desktop run loop that drives an
    # IApplication (client) against an IShell (shell) with a GraphicsDevice - depends on all three.
    add_subdirectory(Desktop)

    if(DRACONIC_BUILD_TESTS)
        add_subdirectory(Tests)
    endif()
endif()
