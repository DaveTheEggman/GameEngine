# Draconic::RuntimePlatformNull — `draconic.runtime.platform.null` (headless IPlatform).

add_library(DraconicRuntimePlatformNull STATIC)
add_library(Draconic::RuntimePlatformNull ALIAS DraconicRuntimePlatformNull)

target_link_libraries(DraconicRuntimePlatformNull PUBLIC Draconic::RuntimePlatform Draconic::Policy)

target_sources(DraconicRuntimePlatformNull
    PUBLIC
        FILE_SET runtime_platform_null_modules TYPE CXX_MODULES FILES
            NullPlatform.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
