# Raptor::RHINull — `raptor.rhi.null` (headless no-op RHI backend, for tests/tools).

add_library(RaptorRHINull STATIC)
add_library(Raptor::RHINull ALIAS RaptorRHINull)

target_link_libraries(RaptorRHINull PUBLIC Raptor::RHI Raptor::Core Raptor::Policy)

target_sources(RaptorRHINull
    PUBLIC
        FILE_SET rhi_null_modules TYPE CXX_MODULES FILES
            NullModule.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
