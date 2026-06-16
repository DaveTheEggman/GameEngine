# Raptor::RuntimePlatform — `raptor.runtime.platform` (IPlatform/IWindow, the raw
# platform service). Interfaces only; native + null backends implement them.

add_library(RaptorRuntimePlatform STATIC)
add_library(Raptor::RuntimePlatform ALIAS RaptorRuntimePlatform)

target_link_libraries(RaptorRuntimePlatform PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorRuntimePlatform
    PUBLIC
        FILE_SET runtime_platform_modules TYPE CXX_MODULES FILES
            Platform.cppm)

# Backends (each only needs the interface).
add_subdirectory(Null)

# Desktop (SDL3) backend — built when SDL3 is available (system/prebuilt).
find_package(SDL3 QUIET)
if(SDL3_FOUND)
    add_subdirectory(Desktop)
else()
    message(STATUS "SDL3 not found — skipping Raptor::RuntimePlatformDesktop")
endif()
