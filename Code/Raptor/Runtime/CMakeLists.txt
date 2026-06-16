# Raptor::Runtime — the `raptor.runtime` module (Context + Subsystems).

add_library(RaptorRuntime STATIC)
add_library(Raptor::Runtime ALIAS RaptorRuntime)

target_link_libraries(RaptorRuntime PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorRuntime
    PUBLIC
        FILE_SET runtime_modules TYPE CXX_MODULES FILES
            RuntimeModule.cppm
            Subsystem.cppm
            Context.cppm
            Plugin.cppm
            PluginHost.cppm)

# Platform service (interface + backends), the client Application loop, and
# scripting integration (separate targets). Platform precedes Client (Client
# links the platform interface).
add_subdirectory(Platform)
add_subdirectory(Client)
add_subdirectory(Script)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
