# Draconic::Runtime - the `draconic.runtime` module (Context + Subsystems).

add_library(DraconicRuntime STATIC)
add_library(Draconic::Runtime ALIAS DraconicRuntime)

target_link_libraries(DraconicRuntime PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicRuntime
    PUBLIC
        FILE_SET runtime_modules TYPE CXX_MODULES FILES
            RuntimeModule.cppm
            Subsystem.cppm
            Context.cppm
            Plugin.cppm
            PluginHost.cppm)

# The client Application loop + scripting integration. (The shell service is now a top-level
# layer - Draconic::Shell - added before Runtime, since Client links it.)
add_subdirectory(Client)
add_subdirectory(DefaultApp)
add_subdirectory(Script)

# Desktop runner (draconic.runtime.desktop): the blocking desktop run loop that drives an
# IApplication (client) against an IShell (shell) with a GraphicsDevice - depends on all three,
# so it follows them. Backend-agnostic (works through IShell); its web runner will live alongside.
add_subdirectory(Desktop)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
