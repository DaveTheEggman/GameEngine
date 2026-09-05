// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Core - Prelude
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
#error "Unsupported compiler."
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
#error "Unsupported platform."
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
#error "Unsupported architecture."
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
// PE has no interposition: every image already gets its own copy of a template's
// statics, so there is nothing to hide.
#define COMPILER_ATTR_HIDDEN
#else
#define COMPILER_ATTR_FORCEINLINE inline __attribute__((always_inline))
#define COMPILER_ATTR_NOINLINE __attribute__((noinline))
#define COMPILER_ATTR_RESTRICT __restrict__
// COMPILER_ATTR_HIDDEN - makes a template (and its function-local statics) PER IMAGE
// on ELF, the way PE already is. ELF's default visibility unifies vague-linkage
// symbols across .so boundaries at load time, which silently gives one copy per
// process and hides exactly the class of bug Windows surfaces (shared-libraries.md
// P5/W1: TypeOf<T>()'s slot). Applied to the templates whose per-image duplication is
// BY DESIGN because the process-wide state behind them lives in an impl unit; the
// Linux shared lane then proves the rendezvous instead of papering over it.
#define COMPILER_ATTR_HIDDEN __attribute__((visibility("hidden")))
#endif

// ENGINE_EXPORT_DATA - for STATIC DATA MEMBERS of types that cross a shared-library
// boundary (Float3::Zero, Color::Red, Guid::Nil...). NOT the classic FOO_API dance and NOT
// needed on functions.
//
// Functions do not need it: on MSVC the shared build generates a .def exporting every
// module-attached function symbol (cmake/GenerateModuleDef.cmake), so the ~2200 functions
// of a library like Core export with no source annotation at all. DATA cannot go that
// route - a .def DATA export still leaves the consumer emitting a direct reference that
// the import library cannot satisfy (verified), because reading imported data needs the
// declaration itself to say so.
//
// One spelling for producer AND consumer, deliberately. The classic export/import macro
// pair cannot work here: there is ONE BMI, read by the library that defines the entity and
// by everyone that imports it, so the macro cannot mean two things. It does not need to -
// MSVC records the dllexport in the BMI and gives consumers the import side automatically
// (verified end to end: consumers link with no dllimport anywhere and read correct values).
// See Documentation/Specs/shared-libraries.md section 5 (P5).
#if COMPILER_MSVC && defined(BUILDSYSTEM_SHARED_LIBS) && BUILDSYSTEM_SHARED_LIBS
#define ENGINE_EXPORT_DATA __declspec(dllexport)
#else
#define ENGINE_EXPORT_DATA
#endif

// Expression-form branch hints are pass-throughs: Clang miscompiles
// __builtin_expect when it is reachable across C++ module units (it conflates
// call sites and reports an ambiguous call). For real hot paths, use the
// C++20 [[likely]] / [[unlikely]] attributes on if/switch statements instead.
#define COMPILER_ATTR_LIKELY(x) (x)
#define COMPILER_ATTR_UNLIKELY(x) (x)

#define CORE_STRINGIFY_(x) #x
#define CORE_STRINGIFY(x) CORE_STRINGIFY_(x)

#endif // FOUNDATION_CORE_PRELUDE_H
