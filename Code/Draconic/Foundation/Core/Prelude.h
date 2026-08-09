// Draconic Core - Prelude
//
// The one classic header allowed to cross the module boundary. Holds everything
// macro-based (platform/compiler detection, attributes, build config), since
// preprocessor macros do not propagate through C++ modules. Each .cppm includes
// this in its global module fragment:
//
//     module;
//     #include "Core/Prelude.h"
//     export module foundation.core:base;
//
// Keep this tiny, dependency-free, and include-once.

#ifndef FOUNDATION_CORE_PRELUDE_H
#define FOUNDATION_CORE_PRELUDE_H

// Placement operator new must be reachable in every TU that instantiates a
// container (GCC binds it at the instantiation site, not where the placement
// new textually appears). Prelude.h is included in every module's global module
// fragment, so this makes it uniformly available. <new> is a tiny header.
#include <new>

// ---------------------------------------------------------------------------
// Compiler detection
// ---------------------------------------------------------------------------
#if defined(__clang__)
#define COMPILER_CLANG 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#error "Draconic: unsupported compiler."
#endif

#if !defined(COMPILER_CLANG)
#define COMPILER_CLANG 0
#endif
#if !defined(COMPILER_GCC)
#define COMPILER_GCC 0
#endif
#if !defined(COMPILER_MSVC)
#define COMPILER_MSVC 0
#endif

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(__EMSCRIPTEN__)
#define PLATFORM_WEB 1
#elif defined(_WIN32)
#define PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define PLATFORM_LINUX 1
#else
#error "Draconic: unsupported platform."
#endif

#if !defined(PLATFORM_WINDOWS)
#define PLATFORM_WINDOWS 0
#endif
#if !defined(PLATFORM_LINUX)
#define PLATFORM_LINUX 0
#endif
#if !defined(PLATFORM_WEB)
#define PLATFORM_WEB 0
#endif

// ---------------------------------------------------------------------------
// Architecture detection
// ---------------------------------------------------------------------------
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ARCH_ARM64 1
#elif defined(__wasm32__) || defined(__wasm64__)
#define ARCH_WASM 1
#else
#error "Draconic: unsupported architecture."
#endif

#if !defined(ARCH_X64)
#define ARCH_X64 0
#endif
#if !defined(ARCH_ARM64)
#define ARCH_ARM64 0
#endif
#if !defined(ARCH_WASM)
#define ARCH_WASM 0
#endif

// Byte order. Both supported architectures run little-endian.
#define ARCH_LITTLE_ENDIAN 1

// ---------------------------------------------------------------------------
// Build configuration
//   BUILD_DEBUG    - asserts on, no/low optimization
//   BUILD_RELEASE  - optimized, asserts on
//   BUILD_SHIPPING - optimized, asserts compiled out
// Define exactly one via the build system; default to BUILD_DEBUG.
// ---------------------------------------------------------------------------
#if !defined(BUILD_DEBUG) && !defined(BUILD_RELEASE) && !defined(BUILD_SHIPPING)
#define BUILD_DEBUG 1
#endif

#if !defined(BUILD_DEBUG)
#define BUILD_DEBUG 0
#endif
#if !defined(BUILD_RELEASE)
#define BUILD_RELEASE 0
#endif
#if !defined(BUILD_SHIPPING)
#define BUILD_SHIPPING 0
#endif

// ---------------------------------------------------------------------------
// Attributes & ABI macros
// ---------------------------------------------------------------------------
#if COMPILER_MSVC
#define COMPILER_ATTR_FORCEINLINE __forceinline
#define COMPILER_ATTR_NOINLINE __declspec(noinline)
#define COMPILER_ATTR_RESTRICT __restrict
#define CORE_EXPORT __declspec(dllexport)
#define CORE_IMPORT __declspec(dllimport)
#else
#define COMPILER_ATTR_FORCEINLINE inline __attribute__((always_inline))
#define COMPILER_ATTR_NOINLINE __attribute__((noinline))
#define COMPILER_ATTR_RESTRICT __restrict__
#define CORE_EXPORT __attribute__((visibility("default")))
#define CORE_IMPORT
#endif

// Core builds as a static library for now; CORE_API is a no-op until we ship
// shared libraries. Plugins (see Library module) will flip this per target.
#define CORE_API

// Expression-form branch hints are pass-throughs: Clang miscompiles
// __builtin_expect when it is reachable across C++ module units (it conflates
// call sites and reports an ambiguous call). For real hot paths, use the
// C++20 [[likely]] / [[unlikely]] attributes on if/switch statements instead.
#define COMPILER_ATTR_LIKELY(x) (x)
#define COMPILER_ATTR_UNLIKELY(x) (x)

#define CORE_STRINGIFY_(x) #x
#define CORE_STRINGIFY(x) CORE_STRINGIFY_(x)

#endif // FOUNDATION_CORE_PRELUDE_H
