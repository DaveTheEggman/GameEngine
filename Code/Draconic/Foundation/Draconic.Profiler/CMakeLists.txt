# Draconic::Profiler - `draconic.profiler` (lightweight hierarchical CPU scope profiler over Core).
# Instrumented across the engine via the DRACONIC_PROFILE_SCOPE macro (Profiler.h); compiles to
# nothing when DRACONIC_PROFILING is off.

add_library(Draconic.Profiler STATIC)
add_library(Draconic::Profiler ALIAS Draconic.Profiler)

target_link_libraries(Draconic.Profiler PUBLIC Draconic::Core Draconic::Policy)

target_sources(Draconic.Profiler
    PUBLIC
        FILE_SET profiler_modules TYPE CXX_MODULES FILES
            Profiler.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Profiler.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
