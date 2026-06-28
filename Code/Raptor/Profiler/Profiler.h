// Raptor::Profiler — instrumentation macros.
//
// Include this header (it's just macros) AND `import raptor.profiler` in a TU that instruments.
// RAPTOR_PROFILE_SCOPE("Name") profiles the enclosing block; the frame macros bracket a frame.
// When RAPTOR_PROFILING is off (shipping builds), every macro compiles to nothing.
#pragma once

#ifndef RAPTOR_PROFILING
#define RAPTOR_PROFILING 0
#endif

#define RAPTOR_PROFILE_CONCAT_(a, b) a##b
#define RAPTOR_PROFILE_CONCAT(a, b)  RAPTOR_PROFILE_CONCAT_(a, b)

#if RAPTOR_PROFILING

#define RAPTOR_PROFILE_SCOPE(name) \
    ::raptor::profiler::ScopedProfile RAPTOR_PROFILE_CONCAT(raptorProfScope_, __LINE__){ (name) }
#define RAPTOR_PROFILE_FRAME_BEGIN() ::raptor::profiler::Profiler::Get().BeginFrame()
#define RAPTOR_PROFILE_FRAME_END()   ::raptor::profiler::Profiler::Get().EndFrame()

#else

#define RAPTOR_PROFILE_SCOPE(name)   ((void)0)
#define RAPTOR_PROFILE_FRAME_BEGIN() ((void)0)
#define RAPTOR_PROFILE_FRAME_END()   ((void)0)

#endif
