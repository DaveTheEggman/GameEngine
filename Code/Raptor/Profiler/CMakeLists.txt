# Raptor::Profiler — `raptor.profiler` (lightweight hierarchical CPU scope profiler over Core).
# Instrumented across the engine via the RAPTOR_PROFILE_SCOPE macro (Profiler.h); compiles to
# nothing when RAPTOR_PROFILING is off.

add_library(RaptorProfiler STATIC)
add_library(Raptor::Profiler ALIAS RaptorProfiler)

target_link_libraries(RaptorProfiler PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorProfiler
    PUBLIC
        FILE_SET profiler_modules TYPE CXX_MODULES FILES
            Profiler.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
