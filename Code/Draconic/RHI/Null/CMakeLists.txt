# Draconic::RHINull — `draconic.rhi.null` (headless no-op RHI backend, for tests/tools).

add_library(DraconicRHINull STATIC)
add_library(Draconic::RHINull ALIAS DraconicRHINull)

target_link_libraries(DraconicRHINull PUBLIC Draconic::RHI Draconic::Core Draconic::Policy)

target_sources(DraconicRHINull
    PUBLIC
        FILE_SET rhi_null_modules TYPE CXX_MODULES FILES
            NullModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
