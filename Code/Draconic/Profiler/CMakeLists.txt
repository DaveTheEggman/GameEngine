# Draconic::Profiler - `draconic.profiler` (lightweight hierarchical CPU scope profiler over Core).
# Instrumented across the engine via the DRACONIC_PROFILE_SCOPE macro (Profiler.h); compiles to
# nothing when DRACONIC_PROFILING is off.

add_library(DraconicProfiler STATIC)
add_library(Draconic::Profiler ALIAS DraconicProfiler)

target_link_libraries(DraconicProfiler PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicProfiler
    PUBLIC
        FILE_SET profiler_modules TYPE CXX_MODULES FILES
            Profiler.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
