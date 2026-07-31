# Draconic::RHINull - `draconic.rhi.null` (headless no-op RHI backend, for tests/tools).

add_library(Draconic.RHI.Null STATIC)
add_library(Draconic::RHINull ALIAS Draconic.RHI.Null)

target_link_libraries(Draconic.RHI.Null PUBLIC Draconic::RHI Draconic::Core Draconic::Policy)

target_sources(Draconic.RHI.Null
    PUBLIC
        FILE_SET rhi_null_modules TYPE CXX_MODULES FILES
            NullModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.RHI.Null.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
