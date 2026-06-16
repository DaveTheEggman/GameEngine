# Raptor::RuntimePlatformNull — `raptor.runtime.platform.null` (headless IPlatform).

add_library(RaptorRuntimePlatformNull STATIC)
add_library(Raptor::RuntimePlatformNull ALIAS RaptorRuntimePlatformNull)

target_link_libraries(RaptorRuntimePlatformNull PUBLIC Raptor::RuntimePlatform Raptor::Policy)

target_sources(RaptorRuntimePlatformNull
    PUBLIC
        FILE_SET runtime_platform_null_modules TYPE CXX_MODULES FILES
            NullPlatform.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
