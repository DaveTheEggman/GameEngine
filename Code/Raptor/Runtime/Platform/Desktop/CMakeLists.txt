# Raptor::RuntimePlatformDesktop — `raptor.runtime.platform.desktop` (SDL3-based
# IPlatform for Windows/Linux/macOS). Linked dynamically; bundled for shipping.

add_library(RaptorRuntimePlatformDesktop STATIC)
add_library(Raptor::RuntimePlatformDesktop ALIAS RaptorRuntimePlatformDesktop)

target_link_libraries(RaptorRuntimePlatformDesktop
    PUBLIC Raptor::RuntimePlatform Raptor::RuntimeClient Raptor::Policy
    PRIVATE SDL3::SDL3)

target_sources(RaptorRuntimePlatformDesktop
    PUBLIC
        FILE_SET runtime_platform_desktop_modules TYPE CXX_MODULES FILES
            SDL3Platform.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
