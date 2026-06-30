# Draconic::RuntimePlatformDesktop — `draconic.runtime.platform.desktop` (SDL3-based
# IPlatform for Windows/Linux/macOS). Linked dynamically; bundled for shipping.

add_library(DraconicRuntimePlatformDesktop STATIC)
add_library(Draconic::RuntimePlatformDesktop ALIAS DraconicRuntimePlatformDesktop)

target_link_libraries(DraconicRuntimePlatformDesktop
    PUBLIC Draconic::RuntimePlatform Draconic::RuntimeClient Draconic::RuntimeGraphics Draconic::Policy
    PRIVATE SDL3::SDL3)

target_sources(DraconicRuntimePlatformDesktop
    PUBLIC
        FILE_SET runtime_platform_desktop_modules TYPE CXX_MODULES FILES
            SDL3Platform.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
