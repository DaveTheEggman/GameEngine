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

# Platform service (interface + null backend), the client Application loop, and
# scripting integration (separate targets). Order matters: the platform
# interface precedes Client (Client links it), and the SDL3 desktop backend
# follows Client (its runner drives Application).
add_subdirectory(Platform)
add_subdirectory(Client)
add_subdirectory(Script)

# Desktop (SDL3) backend — built when SDL3 is available (system/prebuilt).
find_package(SDL3 QUIET)
if(SDL3_FOUND)
    add_subdirectory(Platform/Desktop)
else()
    message(STATUS "SDL3 not found — skipping Raptor::RuntimePlatformDesktop")
endif()

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
