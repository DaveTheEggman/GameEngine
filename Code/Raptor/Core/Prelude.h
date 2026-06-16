// Raptor Core — Prelude
//
// The one classic header allowed to cross the module boundary. Holds everything
// macro-based (platform/compiler detection, attributes, build config), since
// preprocessor macros do not propagate through C++ modules. Each .cppm includes
// this in its global module fragment:
//
//     module;
//     #include "Core/Prelude.h"
//     export module raptor.core:base;
//
// Keep this tiny, dependency-free, and include-once.

#ifndef RAPTOR_CORE_PRELUDE_H
#define RAPTOR_CORE_PRELUDE_H

// Placement operator new must be reachable in every TU that instantiates a
// container (GCC binds it at the instantiation site, not where the placement
// new textually appears). Prelude.h is included in every module's global module
// fragment, so this makes it uniformly available. <new> is a tiny header.
#include <new>

// ---------------------------------------------------------------------------
// Compiler detection
// ---------------------------------------------------------------------------
#if defined(__clang__)
    #define RAPTOR_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define RAPTOR_COMPILER_GCC 1
#elif defined(_MSC_VER)
    #define RAPTOR_COMPILER_MSVC 1
#else
    #error "Raptor: unsupported compiler."
#endif

#if !defined(RAPTOR_COMPILER_CLANG)
    #define RAPTOR_COMPILER_CLANG 0
#endif
#if !defined(RAPTOR_COMPILER_GCC)
    #define RAPTOR_COMPILER_GCC 0
#endif
#if !defined(RAPTOR_COMPILER_MSVC)
    #define RAPTOR_COMPILER_MSVC 0
#endif

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(_WIN32)
    #define RAPTOR_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define RAPTOR_PLATFORM_LINUX 1
#else
    #error "Raptor: unsupported platform."
#endif

#if !defined(RAPTOR_PLATFORM_WINDOWS)
    #define RAPTOR_PLATFORM_WINDOWS 0
#endif
#if !defined(RAPTOR_PLATFORM_LINUX)
    #define RAPTOR_PLATFORM_LINUX 0
#endif

// ---------------------------------------------------------------------------
// Architecture detection
// ---------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
    #define RAPTOR_ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define RAPTOR_ARCH_ARM64 1
#else
    #error "Raptor: unsupported architecture."
#endif

#if !defined(RAPTOR_ARCH_X64)
    #define RAPTOR_ARCH_X64 0
#endif
#if !defined(RAPTOR_ARCH_ARM64)
    #define RAPTOR_ARCH_ARM64 0
#endif

// Byte order. Both supported architectures run little-endian.
#define RAPTOR_LITTLE_ENDIAN 1

// ---------------------------------------------------------------------------
// Build configuration
//   RAPTOR_DEBUG    — asserts on, no/low optimization
//   RAPTOR_RELEASE  — optimized, asserts on
//   RAPTOR_SHIPPING — optimized, asserts compiled out
// Define exactly one via the build system; default to RAPTOR_DEBUG.
// ---------------------------------------------------------------------------
#if !defined(RAPTOR_DEBUG) && !defined(RAPTOR_RELEASE) && !defined(RAPTOR_SHIPPING)
    #define RAPTOR_DEBUG 1
#endif

#if !defined(RAPTOR_DEBUG)
    #define RAPTOR_DEBUG 0
#endif
#if !defined(RAPTOR_RELEASE)
    #define RAPTOR_RELEASE 0
#endif
#if !defined(RAPTOR_SHIPPING)
    #define RAPTOR_SHIPPING 0
#endif

// ---------------------------------------------------------------------------
// Attributes & ABI macros
// ---------------------------------------------------------------------------
#if RAPTOR_COMPILER_MSVC
    #define RAPTOR_FORCEINLINE __forceinline
    #define RAPTOR_NOINLINE    __declspec(noinline)
    #define RAPTOR_RESTRICT    __restrict
    #define RAPTOR_EXPORT      __declspec(dllexport)
    #define RAPTOR_IMPORT      __declspec(dllimport)
#else
    #define RAPTOR_FORCEINLINE inline __attribute__((always_inline))
    #define RAPTOR_NOINLINE    __attribute__((noinline))
    #define RAPTOR_RESTRICT    __restrict__
    #define RAPTOR_EXPORT      __attribute__((visibility("default")))
    #define RAPTOR_IMPORT
#endif

// Core builds as a static library for now; RAPTOR_API is a no-op until we ship
// shared libraries. Plugins (see Library module) will flip this per target.
#define RAPTOR_API

// Expression-form branch hints are pass-throughs: Clang miscompiles
// __builtin_expect when it is reachable across C++ module units (it conflates
// call sites and reports an ambiguous call). For real hot paths, use the
// C++20 [[likely]] / [[unlikely]] attributes on if/switch statements instead.
#define RAPTOR_LIKELY(x)   (x)
#define RAPTOR_UNLIKELY(x) (x)

#define RAPTOR_STRINGIFY_(x) #x
#define RAPTOR_STRINGIFY(x)  RAPTOR_STRINGIFY_(x)

#endif // RAPTOR_CORE_PRELUDE_H
