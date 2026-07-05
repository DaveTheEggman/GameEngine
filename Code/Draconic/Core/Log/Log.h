// Draconic Core - logging macros (classic header).
//
// Convenience front-end over draconic::core::Logf. Include this and `import
// draconic.core;`. The macros are stripped at LogLevel::Fatal+ granularity in
// shipping builds (asserts/logging policy, §4.8).
//
//   DRACONIC_LOG(level, category, "fmt {} {}", a, b);
//   DRACONIC_LOG_INFO("Renderer", "loaded {} meshes", count);

#ifndef DRACONIC_CORE_LOG_H
#define DRACONIC_CORE_LOG_H

#include "Core/Prelude.h"

#define DRACONIC_LOG(level, category, ...) \
    ::draconic::core::Logf((level), (category), __VA_ARGS__)

#if DRACONIC_SHIPPING
    // Strip verbose levels in shipping; keep Warning and above.
    #define DRACONIC_LOG_TRACE(category, ...) ((void)0)
    #define DRACONIC_LOG_DEBUG(category, ...) ((void)0)
    #define DRACONIC_LOG_INFO(category, ...)  ((void)0)
#else
    #define DRACONIC_LOG_TRACE(category, ...) DRACONIC_LOG(::draconic::core::LogLevel::Trace, (category), __VA_ARGS__)
    #define DRACONIC_LOG_DEBUG(category, ...) DRACONIC_LOG(::draconic::core::LogLevel::Debug, (category), __VA_ARGS__)
    #define DRACONIC_LOG_INFO(category, ...)  DRACONIC_LOG(::draconic::core::LogLevel::Info,  (category), __VA_ARGS__)
#endif

#define DRACONIC_LOG_WARNING(category, ...) DRACONIC_LOG(::draconic::core::LogLevel::Warning, (category), __VA_ARGS__)
#define DRACONIC_LOG_ERROR(category, ...)   DRACONIC_LOG(::draconic::core::LogLevel::Error,   (category), __VA_ARGS__)
#define DRACONIC_LOG_FATAL(category, ...)   DRACONIC_LOG(::draconic::core::LogLevel::Fatal,   (category), __VA_ARGS__)

#endif // DRACONIC_CORE_LOG_H
