// Raptor Core — logging macros (classic header).
//
// Convenience front-end over raptor::core::Logf. Include this and `import
// raptor.core;`. The macros are stripped at LogLevel::Fatal+ granularity in
// shipping builds (asserts/logging policy, §4.8).
//
//   RAPTOR_LOG(level, category, "fmt {} {}", a, b);
//   RAPTOR_LOG_INFO("Renderer", "loaded {} meshes", count);

#ifndef RAPTOR_CORE_LOG_H
#define RAPTOR_CORE_LOG_H

#include "Core/Prelude.h"

#define RAPTOR_LOG(level, category, ...) \
    ::raptor::core::Logf((level), (category), __VA_ARGS__)

#if RAPTOR_SHIPPING
    // Strip verbose levels in shipping; keep Warning and above.
    #define RAPTOR_LOG_TRACE(category, ...) ((void)0)
    #define RAPTOR_LOG_DEBUG(category, ...) ((void)0)
    #define RAPTOR_LOG_INFO(category, ...)  ((void)0)
#else
    #define RAPTOR_LOG_TRACE(category, ...) RAPTOR_LOG(::raptor::core::LogLevel::Trace, (category), __VA_ARGS__)
    #define RAPTOR_LOG_DEBUG(category, ...) RAPTOR_LOG(::raptor::core::LogLevel::Debug, (category), __VA_ARGS__)
    #define RAPTOR_LOG_INFO(category, ...)  RAPTOR_LOG(::raptor::core::LogLevel::Info,  (category), __VA_ARGS__)
#endif

#define RAPTOR_LOG_WARNING(category, ...) RAPTOR_LOG(::raptor::core::LogLevel::Warning, (category), __VA_ARGS__)
#define RAPTOR_LOG_ERROR(category, ...)   RAPTOR_LOG(::raptor::core::LogLevel::Error,   (category), __VA_ARGS__)
#define RAPTOR_LOG_FATAL(category, ...)   RAPTOR_LOG(::raptor::core::LogLevel::Fatal,   (category), __VA_ARGS__)

#endif // RAPTOR_CORE_LOG_H
