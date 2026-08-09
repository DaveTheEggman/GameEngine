# Foundation::Profiler - `draconic.profiler` (lightweight hierarchical CPU scope profiler over Core).
# Instrumented across the engine via the DRACONIC_PROFILE_SCOPE macro (Profiler.h); compiles to
# nothing when DRACONIC_PROFILING is off.

add_library(Profiler STATIC)
add_library(Foundation::Profiler ALIAS Profiler)

target_link_libraries(Profiler PUBLIC Foundation::Core Foundation::Policy)

target_sources(Profiler
    PUBLIC
        FILE_SET profiler_modules TYPE CXX_MODULES FILES
            Profiler.cppm)

