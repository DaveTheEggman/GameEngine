# Raptor Core — Implementation Plan

> Status: **In progress** — Core implemented on Linux (Clang 21 + GCC 15), all
> ten subsystems functional with tests green under ASan/UBSan/TSan. Per-subsystem
> status in §8; outstanding work consolidated in §10. · Owner: TBD · Last updated: 2026-06-15
>
> **Caveat:** §4 (per-module first-pass scope) is the original design intent and is
> *not* kept in lockstep with the implementation — §8/§10 are authoritative.

The Core library is the foundation every other engine module builds on. It owns
the engine's fundamental types, platform abstraction, memory management,
containers, and the diagnostic/reflection facilities used everywhere else.
Nothing in Core may depend on a higher-level engine module; Core depends only on
the C++ standard library (minimally) and the platform SDKs.

---

## 1. Ground Rules

These constraints apply to **all** Core code and are non-negotiable unless this
document is revised.

| Area | Decision | Rationale |
|------|----------|-----------|
| Language | **C++23** | Concepts, `std::expected`-style patterns, `<bit>`, `if consteval`, expanded `constexpr`, `[[assume]]`. |
| Code organization | **C++ modules** | `Raptor.Core` named module; subsystems are partitions re-exported by `CoreModule.cppm`. Faster builds, real encapsulation, no header-order fragility. See §2. |
| STL usage | **Minimal** | Roll our own containers/strings/memory. STL allowed only for compile-time/meta headers (`<type_traits>`, `<utility>`, `<concepts>`, `<bit>`, `<limits>`, `<initializer_list>`). No `std::vector`, `std::string`, `std::shared_ptr`, iostreams. |
| Exceptions | **Disabled** (`-fno-exceptions` / `/EHs-c-`) | Predictable control flow and code size. Errors flow through result/status types. |
| C++ RTTI | **Disabled** (`-fno-rtti` / `/GR-`) | No `dynamic_cast`/`typeid`. The custom `Core/RTTI` system replaces it. |
| Build system | **CMake** (≥ 3.28) | Cross-platform Linux + Windows; required for stable `CXX_MODULES` support. |
| Allocation | **Explicit** | No hidden heap allocation. Containers take an allocator. Global `new`/`delete` routed through Core memory. |
| Naming | **`raptor` / `raptor::core` namespaces, PascalCase types (`I`-prefixed interfaces), `m_` members, `RAPTOR_` macros, `.cppm` files** | See §7 for the full convention. |
| Math | **Row-major matrices, row vectors, XNA-style projections** | Vector-on-left (`v * M`); right-to-left transform composition. See §7. |

**Initial platforms:** Linux (primary dev target) and Win32. The `System` layer
isolates everything platform-specific behind a common interface.

---

## 2. C++ Modules Strategy

Core is built as a single named module **`Raptor.Core`** composed of **module
partitions**, one per subsystem. The primary interface unit lives at the Core
root and re-exports the partitions.

```
Code/Raptor/Core/
  CoreModule.cppm        // primary interface unit:  export module Raptor.Core;
                         //   export import :Base;  export import :Memory;  ... (re-exports all partitions)
  Prelude.h              // classic header: macros + platform/compiler detection (NOT a module)
  Base.cppm              // partition :Base — fundamental exported types & utilities
  Memory/Memory.cppm     // partition :Memory — export module Raptor.Core:Memory;
  Containers/Containers.cppm
  ...
```

**Consumers** simply write `import Raptor.Core;` and get the whole surface
(downstream engine modules can later import finer-grained pieces if we expose
them). Internally, partitions `import :OtherPartition;` to use each other.

### 2.1 The macro problem — `Prelude.h`
Preprocessor macros **do not** cross module boundaries. Anything macro-based must
live in a classic header:
- Platform/compiler detection (`RAPTOR_PLATFORM_*`, compiler/arch/endian macros).
- Attribute & ABI macros (`RAPTOR_API`, `FORCEINLINE`, `NOINLINE`, `RAPTOR_ALIGN`, `RAPTOR_LIKELY/UNLIKELY`, `RAPTOR_NODISCARD`, `RAPTOR_DEPRECATED`).
- Build-config switches (`RAPTOR_DEBUG/RELEASE/SHIPPING`) and the assertion macros from `Debug`.

Each `.cppm` includes `Prelude.h` in its **global module fragment**:
```cpp
module;                     // global module fragment
#include "Core/Prelude.h"   // macros, platform detection
export module Raptor.Core:Memory;
import :Base;
// ... exported declarations
```
Keep `Prelude.h` tiny, dependency-free, and include-once. It is the one header
allowed to leak across the module boundary.

### 2.2 Partitions vs. separate modules — **decided: partitions**
Core is **one `Raptor.Core` module composed of partitions** (`:Base`, `:Memory`,
…), re-exported by `CoreModule.cppm`. Simplest to consume and matches the "one
`CoreModule.cppm` that exports things" intent. Accepted trade-off: a partition
change can trigger rebuilds of dependents within the module; revisit only if
build times become a problem.

### 2.3 Build notes (CMake)
- Use `target_sources(Raptor.Core PUBLIC FILE_SET CXX_MODULES FILES …)` for `.cppm` units; CMake ≥ 3.28 + a recent Clang/MSVC (and GCC 14+) required.
- Module BMIs are compiler-specific — pin toolchain versions in CI.
- `Prelude.h` ships via a normal `FILE_SET HEADERS` / include dir.
- Verify both Linux (Clang/libc++) and Win32 (MSVC) module builds early; this is the riskiest part of Phase 0.

---

## 3. Module Map & Dependency Order

The folders under `Code/Raptor/Core/` in **build/dependency order** — earlier
modules must not depend on later ones.

```
Prelude.h (header)  +  :Base (fundamental types, Result, utilities)
  ├─ Debug       ← assertions, diagnostics, crash handling
  ├─ Memory      ← allocators, smart pointers, allocation tracking
  ├─ Math        ← scalar/vector/matrix/quaternion, SIMD
  └─ System      ← platform abstraction (Linux/, Win32/): time, files, OS threads, page alloc, raw dynamic-lib calls
       ├─ Containers  ← array, string, hash map, span (needs Memory + Debug)
       ├─ Threading   ← threads, sync primitives, atomics, job system (needs System + Memory + Containers)
       ├─ IO          ← streams, paths, file abstraction, serialization (needs System + Containers)
       ├─ Log         ← logging frontend + sinks (needs Containers + IO + System)
       ├─ Library     ← dynamic library / plugin loading on top of System's raw calls
       └─ RTTI        ← type registry, reflection, type info (needs Containers + Memory + :Base)
```

Dependency rule of thumb: **lower modules know nothing about higher ones.**

> **Note on `Base`:** the fundamental types/utilities have no dedicated folder in
> the current tree, so they live at the Core root as `Base.cppm` (partition
> `:Base`) alongside `CoreModule.cppm` and `Prelude.h`. Rename/relocate if you'd
> rather give them a folder.

---

## 4. Module Responsibilities & First-Pass Scope

### 4.0 Prelude.h + Base  — *the foundation*
Split across the macro header and the `:Base` partition (see §2.1):
- **`Prelude.h` (macros only):** platform/compiler/arch/endian detection, attribute & ABI macros, build-config switches.
- **`:Base` (exported):**
  - Primitive type aliases: `i8/i16/i32/i64`, `u8/u16/u32/u64`, `f32/f64`, `usize`, `isize`, `byte`.
  - Fundamental utilities: `Move`/`Forward`, `Swap`, `ArrayCount`, min/max/clamp, bit helpers, non-copyable/non-movable mixins.
  - **Result/Status types:** `Result<T, E>` (an `expected`-like type) + `ErrorCode`/`Status` scheme — the project-wide error vocabulary, since exceptions are off.
  - Core concepts used widely (e.g. `Integral`, `Trivial`, `Allocator`).

### 4.1 Debug
- `RAPTOR_ASSERT` / `RAPTOR_ASSERT_MSG` / `RAPTOR_VERIFY` / `RAPTOR_CHECK` / `RAPTOR_ENSURE` (non-fatal, returns the condition) / `RAPTOR_UNREACHABLE`.
- **Implemented as a classic header (`Debug/Assert.h`) + `.cpp`, *not* a module partition.** Assert macros must be includable everywhere — including other module units' global-module-fragments — and `:base` itself asserts `Result` preconditions; a `:debug` partition would create a `:base`↔`:debug` import cycle. The reporting functions (`ReportAssertFailure`, `ReportFatal`, `Set/GetAssertHandler`) have external linkage in the global module, callable from any TU with no module dependency. *(This is the one subsystem that is header+cpp rather than a partition.)*
- Assertion handler hook (tests/tools intercept; default prints to stderr then traps), `RAPTOR_DEBUGBREAK`, fatal-error path. Stack-trace capture (delegates to `System`) is TODO. Asserts compiled out in shipping; `RAPTOR_CHECK` stays in all configs.

### 4.2 Memory
- Allocator interface (concept- and/or vtable-based): `Allocate`, `Free`, `Reallocate`, aligned variants.
- Concrete allocators: system/heap, linear/arena, stack, pool/freelist, frame/double-buffered.
- Global allocation routing + `new`/`delete` overrides; `RAPTOR_NEW`/`RAPTOR_DELETE` carrying file/line.
- Smart pointers (no `std::` equivalents): `UniquePtr` (sole ownership, default); **intrusive `RefPtr`/`WeakRefPtr`** via a `RefCounted` base (strong+weak counters embedded — strong→destroy, weak→free) as the default for shared objects; non-intrusive `SharedPtr` only for types we can't modify.
- `RefCounted` is a **lifetime-only** base. The reflection root `Object` derives from it (`Object : public RefCounted`, §4.10), so every polymorphic object is ref-counted and held via `RefPtr<Object>`. Value types reflect without any base.
- Allocation tracking / leak detection / budgets (debug), memory tagging by subsystem.
- Utilities: `Memcpy/Memset/Memmove` wrappers, alignment helpers, placement construct/destruct.

### 4.3 Math
- Scalars & constants, fast/approx functions, `constexpr` where possible.
- `Vec2/3/4`, `Mat3/Mat4`, `Quat`, `Transform`, `Plane`, `AABB`, `Rect`, color types.
- SIMD path (SSE/AVX, NEON) behind a scalar fallback; alignment-aware.
- RNG, easing/interpolation, geometric helpers.
- **Decision:** row- vs column-major and handedness — pin early (§7).

### 4.4 System  *(platform abstraction; `Linux/` + `Win32/` impls)*
- High-resolution time & clocks, sleep, cycle counters.
- Low-level file ops (open/read/write/seek/stat, dir iteration) — the primitive layer `IO` builds on.
- OS threading primitives (raw thread create/join, mutex, condvar, semaphore, TLS) — wrapped by `Threading`.
- **Raw dynamic-library calls** (`dlopen`/`dlsym`/`dlclose`, `LoadLibrary`/`GetProcAddress`/`FreeLibrary`) — the primitive the `Library` module wraps.
- Environment/args, CPU & memory info, page allocation (`VirtualAlloc`/`mmap`), console/stdout, debugger detection, stack-trace backend.
- Pattern: common partition interface in `System/`, per-platform `.cpp`/partition-impl in `System/Linux` and `System/Win32`, selected by build.

### 4.5 Containers
- `Array` (dynamic), `FixedArray`, `StaticArray`, `Span`/`View`.
- **Strings — `String` is UTF-8** (`char8_t`), the primary text type and engine currency: it matches the source we port from (Sedulous), the vendored C libraries (stb/cgltf/ufbx/SDL/Vulkan/DXC), text formats (glTF/fonts/XML/JSON), Wren scripting, and POSIX file IO — so almost no boundary transcodes. `WideString` (`char16_t`, UTF-16) is the secondary type, used only at the Win32/DXGI/DXC edge. Each has a matching `StringView`/`StringBuilder`. `ToUTF8`/`ToWide` transcode at that one edge (invalid sequences → U+FFFD). *(Originally `String` was wide for Win32 alignment; reversed once it proved to fight every other boundary — see §7.)*
- `HashMap`/`HashSet` (open addressing), ordered `Map` if needed.
- `RingBuffer`, `Deque`, `IntrusiveList`, `BitArray`, `SparseArray`/slot-map, `Optional`, `Pair`/`Tuple`.
- All allocator-aware; range-for support; `constexpr`-friendly where reasonable.
- Hashing utilities (`Hash<T>`, FNV/xxHash) live here or in `:Base` (the foundation partition — not `Library`, which is for dynamic library loading).

### 4.6 Threading
- `Thread`, naming/affinity, `ThreadLocal`.
- Sync: `Mutex`, `RecursiveMutex`, `SharedMutex`, `ConditionVariable`, `Semaphore`, `SpinLock`, scoped guards.
- `Atomic<T>` wrappers + fences (or thin layer over `<atomic>`).
- Lock-free building blocks: MPMC/SPSC queues.
- **Job/Task system:** worker pool, task graph/fibers, `JobHandle`, dependencies — likely phase-2.

### 4.7 IO
- `Stream` abstraction (read/write/seek), `FileStream`, `MemoryStream`, `BufferedStream`.
- `Path` manipulation, `FileSystem` facade over `System` file ops; virtual/mounted FS (later).
- Binary (de)serialization primitives, endian handling.
- Async IO hooks (later; ties into `Threading`).

#### Serialization contract (lives in Core; backends do not)
A type describes its data **once** and the serializer runs it in either
direction — no separate save/load paths.
- **`ISerializer`** — pure interface. Direction query (`IsLoading()`/`IsSaving()`) plus the one primitive backends must implement: `SerializeRaw(void* data, usize size) -> Status`. No data members.
- **`Serializer`** — concrete base extending `ISerializer`: holds the `Direction` enum + shared bookkeeping (stream pointer, version, error `Status`). Concrete backends extend **this**, not the interface.
- **`Serialize(ISerializer&, T&)`** — named free functions (not an operator) carry the actual work; ADL lets any type opt in. Core ships overloads for primitives and its own types (`Array`, `String`, math, etc.).
- Errors flow through `Result`/`Status` (exceptions are off); a short read on load is recoverable, not a crash.
- Versioning hook (`Version()`) so old data loads into new code.

**Out of Core — external `Serialization` module:** concrete backends
(`BinarySerializer`, JSON/text), the **RTTI-driven auto-walker** (serialize via
reflected fields, no hand-written `Serialize`), schema, versioning/migration,
object-graph/pointer fixup, asset references. Core defines *how a type exposes
its data*; the external module decides *what format it lands in and how it
evolves*.

### 4.8 Log
- Frontend: `RAPTOR_LOG(category, level, fmt, ...)` (macro → header), levels (Trace→Fatal), categories/channels.
- Sinks: console (colored), file, debugger/OutputDebugString, in-memory ring (for tools).
- Own typesafe formatter (no iostreams), numbers via `<charconv>`, `consteval` format-string checks — see §7.
- Thread-safe; compiled-out levels in shipping.

### 4.9 Library  *(dynamic library / plugin loading)*
Built **on top of `System`'s raw dynamic-lib calls**. Kept as its own module —
**plugins are a planned feature**:
- `DynamicLibrary` RAII handle: load/unload, `GetSymbol<T>(name)` returning typed function pointers, error reporting via `Result`.
- Search-path handling, platform extension resolution (`.so`/`.dll`/`.dylib`), versioned names.
- Foundation for the future **plugin/module system**: discover, load, init/shutdown lifecycle, dependency ordering, hot-reload.

### 4.10 RTTI  *(custom reflection — replaces C++ RTTI)*
The reflection system is the backbone of serialization, the future editor, and
**scripting bindings** (the goal: reflect almost every type and bind it to
arbitrary script languages generically). Lineage: Unreal `UClass/UProperty/
UFunction`, Godot `ClassDB`, RTTR — adapted to our constraints (no C++ RTTI, no
exceptions, custom containers, `Result`/`Status`).

**Layering:**
```
Descriptors   TypeInfo / PropertyInfo / MethodInfo / EnumInfo   ← static data per type
Registry      raptor::core type registry: name/namespace/TypeId → TypeInfo*
Type erasure  Variant (owned value) + Instance (borrowed target)
Invocation    generated thunks: unpack Variants → call real fn → pack result
Bindings      per-language bridges (Lua/Python/C#)              ← OUTSIDE Core
```

#### Descriptors
- **`TypeInfo`** — one static descriptor per reflected type: `TypeId`, `name`, `namespaceName`, size/align, `TypeFlags` (Class/Enum/Pod/Polymorphic/Pointer…), `base` (single-inheritance chain), `TypeOps`, and spans of properties/methods/enumerators/attributes.
- **`TypeOps`** — lifecycle as function pointers (default/copy/move construct, destruct, equals, hash), generated once per `T`. This is what lets `Variant` construct/copy/destroy a type it knows only as `const TypeInfo*`, with no C++ RTTI.
- **`TypeId`** — 64-bit hash (FNV/xxHash) of the fully-qualified name. Stable across builds/sessions → reused as the serialization type tag. Registry asserts on collisions.
- **`PropertyInfo`** — name, type, flags (ReadOnly/Transient/ScriptHidden…), type-erased `Get(Instance) -> Variant` / `Set(Instance, Variant) -> Status`.
- **`MethodInfo`** — name, return type, param list, flags (Static/Const/Virtual), `Invoke(Instance, Span<Variant>) -> Result<Variant>`.
- **`ParamInfo`** — `{ const TypeInfo* type; StringView name /* optional */; Variant defaultValue; ParamFlags flags; }`. Types are auto-deduced from the signature; **names and defaults are not recoverable from C++ and default to empty/none** (see "Parameter names" below).
- **`Attribute`** — freeform `{ StringView key; Variant value; }` metadata bag on type/property/method (script-name overrides, editor ranges, "don't bind", etc.) — keeps policy out of Core.

#### Variant vs Instance (decided)
- **`Variant`** — *always owns* its value (SBO inline + heap fallback), carries `const TypeInfo*`, copy/move/destroy via `TypeOps`. For objects it owns a *pointer* (`Entity*`/`RefPtr<Entity>`), not the object. Mismatched `TryGet<T>()` → `Result`, never throws.
- **`Instance`** — borrowed `{ void* ptr; const TypeInfo* type; }`, non-owning, the `this`-target for property/method access. Constructible from any `T*`; `Variant::ToInstance()` bridges the other way.
- **No reference-mode on `Variant`** — ownership stays a compile-time/static property, not a runtime flag; avoids ambiguous copies and dangling refs.

#### Invocation (thunks)
The concrete signature is captured at **registration** time inside a template
that synthesizes a plain function pointer: a function-traits helper builds
`ParamInfo` from the signature and emits an `Invoke` that checks arg count/types,
pulls each `Variant` into the right C++ type, calls through the member pointer,
and packs the return into a `Variant`. Type mismatches return
`Status::ArgTypeMismatch` (surfaced as a script error at the binding boundary).
All template machinery runs only at registration — never in a hot path.

#### Parameter names (and defaults)
C++23 exposes parameter *types* and arity but **not parameter names** (or default
values) — they aren't part of the function type. So `Invoke` and positional
binding work fully without them; names are only needed for keyword args (Python
kwargs) and tooling (tooltips/autocomplete/docs). Sources, phased:
1. **Positional (default)** — `ParamInfo::name` empty; bindings match by index. Works day one.
2. **Explicit at registration** — supply via the builder where it matters: `.Method("Translate", &Transform::Translate, { "delta", "space" })`. Same path carries default values.
3. **Build-time codegen (later, optional)** — a **libclang-based reflection tool** parses headers and emits the `RegisterTypes()` bodies with names, defaults, doc comments, and attributes pulled from the AST (the Unreal-Header-Tool path; pairs with our explicit-registration decision and can eliminate most hand-written `RAPTOR_REFLECT` blocks).
4. **Future** — C++26 static reflection (P2996) will provide names natively; `ParamInfo::name` is designed to be filled by any source without API churn.

Design rule: an **absent name is a normal, supported state** — the system is fully
functional without it, and the codegen tool is a quality-of-life upgrade, not a
prerequisite.

#### Object model — `Object : public RefCounted` (decided)
Two flavors of reflected type:
- **Value types** (`Vec3`, `Transform`, PODs) — no base, no intrusion; `TypeInfo` via static `TypeOf<T>()`. Marshalled by value through `Variant`. Most of Core.
- **`Object`** — the polymorphic reflection root, **derives from `RefCounted`** (§4.2): `virtual const TypeInfo* GetType() const`, enabling runtime type from a base pointer and `Cast<T>`/`IsA` (replacing `dynamic_cast`). Marshalled by handle (`Instance` / `RefPtr<Object>`).
- Because every `Object` is ref-counted, the binding/registry layer holds any object through a single uniform handle — **`RefPtr<Object>`** — with automatic lifetime in the script VM (no dangling, no second handle type). Invariant: **`Object`s are owned by `RefPtr`**.
- `RefCounted` itself is *not* a reflected type; the reflected `base` chain stays single-inheritance and starts at `Object`.

#### Registration — explicit, per-module (decided)
- Each module exposes `RegisterTypes(TypeRegistry&)`, called in a defined order at startup. No static self-registration: deterministic, no static-init-order trap, and plugins can register/unregister their types cleanly on `dlopen`/unload (ties into `Library`, §4.9).
- Authoring via a macro front-end (`RAPTOR_REFLECT`, in a header) + a fluent builder in the `:RTTI` partition:
  ```cpp
  RAPTOR_REFLECT(raptor::math::Transform) {
      Reflect<Transform>()
          .Property("position", &Transform::position)
          .Method("Translate", &Transform::Translate)
          .Attribute("scriptName", "Transform");
  }
  ```

#### Scripting boundary (out of Core)
Per-language bridges live in an external **`Scripting`** module, never in Core
(same discipline as Serialization). A bridge just walks the registry and mirrors
it into the VM: metatable per `TypeInfo`, `__index`/`__newindex` → property
Get/Set, methods → closures over `Invoke`, namespaces → nested tables/modules.
The only per-language code is `ScriptValue ↔ Variant` marshalling + object-handle
wrapping. `raptor::core` reflection knows nothing about Lua/Python/C#.

#### Build order (phased)
(a) `TypeInfo` + registry + `TypeId` + `Cast`/`IsA`; (b) `Variant`/`Instance`;
(c) properties; (d) methods/`Invoke`; (e) enums + attributes; (f) generic
container reflection (iterate `Array<T>` etc. from script). The external
`Scripting` module begins after (d).

---

## 5. Cross-Cutting Concerns

- **Allocator threading:** define which container/allocator ops are thread-safe vs caller-synchronized; document per type.
- **Error model:** `Result<T,E>` for recoverable; assert/fatal for programmer errors. No silent failure. Codified in `:Base`.
- **constexpr/compile-time:** prefer `constexpr` + concepts over SFINAE; keep heavy templates off hot import paths.
- **Hot-path discipline:** no hidden allocations; no virtual calls in tight loops without reason; mark intent.
- **Macro hygiene:** macros only where modules can't reach (detection, attributes, assert/log front-ends). Keep them in `Prelude.h`/per-module headers, prefixed, minimal.
- **Determinism:** flag where math/threading must be deterministic (matters later for networking/replays).

---

## 6. Tooling, Build & Quality

- **CMake layout:** `Raptor.Core` module target (static lib initially) using `FILE_SET CXX_MODULES`. Toolchain files for Linux/Win32. Warnings as errors; high warning level.
- **Compiler flags:** `-fno-exceptions -fno-rtti` (+ MSVC equivalents) enforced on the Core target.
- **Toolchain pinning:** module BMIs are compiler-specific — pin Clang/MSVC/GCC versions in CI.
- **ThirdParty:** `Code/ThirdParty/` for vendored deps — keep Core light; candidates: xxHash, a SIMD/math helper, **doctest** for tests. No `fmt` (own formatter over `<charconv>`).
- **Testing:** unit tests per module from day one. CI on Linux + Windows. Sanitizers (ASan/UBSan/TSan) for test runs.
- **Static analysis / formatting:** `.clang-format` + `.clang-tidy` checked in before first real code.
- **Docs:** keep this plan current; per-module notes as modules land.

---

## 7. Open Decisions (resolve before / during Phase 0)

### Decided

- **Module granularity** → **partitions** of one `Raptor.Core` (§2.2).
- **`Library`** → **kept** as the dynamic-lib/plugin module, plugins planned (§4.9).
- **Naming convention:**
  - Namespaces **lowercase**: `raptor` root, `raptor::core` for Core, nested per subsystem as needed.
  - **Types** PascalCase (`DynamicLibrary`, `HashMap`). Functions/methods PascalCase (engine style).
  - **Interface types** prefixed **`I`** (`ISerializer`, `IAllocator`).
  - **Members** `m_`-prefixed (`m_count`); other casing TBD only at member-vs-local granularity.
  - **Macros** `RAPTOR_`-prefixed, SCREAMING_CASE (`RAPTOR_ASSERT`).
  - **File extension** `.cppm` for module units; `.h` for the few classic headers (`Prelude.h`); `.cpp` for non-module impl.
- **Math conventions:** **row-major** storage, **row vectors** (vector-on-left: `v * M`), transforms compose **left-to-right** as written (`v * World * View * Proj`), **XNA-style projections** (matches the row-vector convention; right-handed view space, `[0,1]` NDC depth — confirm depth range at implementation).

- **Formatter** → **build our own** typesafe formatter, with numeric conversions delegated to **`std::to_chars`/`std::from_chars` (`<charconv>`)**. No `fmt` dependency; allocation- and exception-free; `consteval` format-string checks; writes into our `String`/`StringBuilder`. (Used by `Log`, §4.8.)
- **Test framework** → **doctest** — first-class `-fno-exceptions` mode (`DOCTEST_CONFIG_NO_EXCEPTIONS`), minimal compile-time impact, tests embeddable next to code.
- **Smart pointers** → **tiered, intrusive default**: `UniquePtr` for sole ownership; intrusive `RefPtr`/`WeakRefPtr` (counters embedded in a `RefCounted` base — strong→destroy, weak→free) as the default for shared engine objects; non-intrusive `SharedPtr` kept only as an escape hatch for non-modifiable types. (§4.2)

- **`Base` location** → **root `Base.cppm`** partition (`:base`), alongside `CoreModule.cppm` and `Prelude.h`. No dedicated folder.
- **Primary string encoding** → **`char8_t` (UTF-8)** for `String` (the engine currency). `WideString` (`char16_t`, UTF-16) is the secondary type, materialized only at the Win32/DXGI/DXC edge (e.g. `CreateWindowW`, DXGI adapter descriptions, DXC `LPCWSTR`); `System`/`IO` paths are UTF-8 and transcode at the Win32 call. *(Decision reversed from the original wide-`String` proposal: UTF-8 alignment with Sedulous, the C deps, text formats, Wren, and POSIX outweighs Win32 alignment, which was the sole argument for wide and is already handled by edge transcoding.)*

### Open

*(none — all Phase 0 decisions resolved)*

---

## 8. Implementation Roadmap

**Phase 0 — Project scaffolding**  *(bootstrapped — builds & tests green on Linux/Clang 21 + GCC 15)*
- ✅ CMake skeleton with a `raptor.core` **module** target; `import raptor.core;` builds. (`CMakeLists.txt`, `CMakePresets.json`, `Code/Raptor/Core/CMakeLists.txt`)
- ✅ `Prelude.h` (platform/compiler/arch detection, attributes, build config) + `CoreModule.cppm` re-exporting `:base` + `Base.cppm` stub (types + Min/Max/Clamp/ArrayCount).
- ✅ `.clang-format`, `.clang-tidy`, doctest wired (`-fno-exceptions` mode), no-exc/no-rtti + warnings-as-errors via the `Raptor::Policy` interface target.
- ✅ Resolved all §7 open items.
- ⬜ Remaining: CI on both platforms; verify the MSVC/Win32 module build.

**Phase 1 — Foundation**
- ✅ `:base` — fundamental types, `Move`/`Forward`/`Swap`, `NonCopyable`/`NonMovable`, `ErrorCode`/`Status`/`Result<T,E>`. Green on Clang 21 + GCC 15.
- ✅ `Debug` — asserts (`RAPTOR_ASSERT`/`VERIFY`/`CHECK`/`ENSURE`/`UNREACHABLE`), handler hook, fatal path (classic header+cpp, §4.1).
- ✅ `Memory` — `IAllocator` + `SystemAllocator` + `LinearAllocator`, alignment/raw-mem utils, `UniquePtr`, `RefCounted` (strong+weak) + `RefPtr` + `WeakRefPtr`, `MakeUnique`/`MakeRef`. Allocators: System/Linear/Pool/Stack + `TrackingAllocator` (leak detection). Clean under ASan/UBSan. **Deferred:** frame/double-buffered allocator, memory tagging by subsystem, per-allocation byte tracking.
- 🟡 `System` — time, system info, page allocation, file primitives, console out, raw dynamic-lib calls (`dlopen`/`dlsym`/`dlclose`). Backend split: classic `SystemBackend.h` + `System/Linux/LinuxSystem.cpp`; `:system` partition forwards/re-exports. **Deferred:** Win32 backend, stack trace.

**Phase 2 — Data & Diagnostics**
- 🟡 `Containers` — ✅ `Span`, `Array`, `RingBuffer`; ✅ `String` (UTF-8) + `WideString` (UTF-16, Win32 edge); ✅ `Hash<T>`, `HashMap`, `HashSet`. ✅ SSO, UTF-8↔UTF-16 transcoding, `StringBuilder`, `IntrusiveList`.
- ✅ `Log` — ✅ own typesafe formatter (`FormatBuffer` + `FormatTo` over `<charconv>`, `{}` substitution, brace escapes); ✅ frontend (`LogLevel`, `Logf`, `RAPTOR_LOG_*` macros), level filtering, `ILogSink` + `ConsoleSink` + `FileSink`, **thread-safe** global `Logger` (atomic level + mutex-guarded sinks/dispatch, verified under TSan). **Complete.** (`RingLogSink`, wide/UTF-8 args, compile-time format-arg-count checks done.)
- 🟡 `Math` — ✅ scalars/constants, `Vec2/3/4` (+`Normalized` for all), `Mat4` (row-major, row vectors, XNA-style RH projections/look-at, `Determinant`/`Inverse`), `Quat` (axis-angle, Hamilton product, rotate, matrix, `Slerp`), `Transform` (S·R·T). Conventions cross-checked in tests. **Deferred:** SIMD. (`AABB`/`Plane`/`Rect`, `Random`/PCG32, `Color` done.)

**Phase 3 — Concurrency, IO & Loading**
- 🟡 `Threading` — ✅ `Thread` (callable, join/detach), `Mutex`/`SpinLock`/`SharedMutex` + `ScopedLock`/`ScopedSharedLock`, `Semaphore`, `ConditionVariable`, `Atomic<T>`. Backend split: classic `ThreadBackend.h` + `Threading/Linux/LinuxThread.cpp` (pthreads, opaque mutex/cond storage). Clean under TSan. ✅ `JobSystem` (worker pool, enqueue any callable, `WaitForAll`). **Deferred:** Win32 backend, lock-free queues, task-graph dependencies.
- 🟡 `IO` — ✅ `IStream` (read/write/seek/tell/size + typed `ReadValue`/`WriteValue`), `FileStream`, `MemoryStream`. ✅ serialization contract: `ISerializer` (pure, direction + `SerializeRaw`), `Serializer` base (direction/version/sticky status), `BinarySerializer` over `IStream`, and `Serialize(ISerializer&, T&)` for arithmetic/enum/math/`String`/`Array` + user types (one path, both directions). ✅ `ReadFile`/`WriteFile` + directory ops; ✅ `IFileSystem`/`NativeFileSystem`/`VirtualFileSystem` (mount by longest-prefix). **Deferred:** RTTI-driven auto-walker (external), async IO, endian-aware serializer wiring. (`BufferedStream`, `Path`, endian helpers, VFS done.)
- 🟡 `Library` — ✅ `DynamicLibrary` (RAII load/unload, move-only, typed `GetSymbol<T>`, `Status`-based errors) over System's raw dynamic-lib calls. Tested against a real built plugin (`Tests/TestPlugin.cpp`). **Deferred:** platform name/extension resolution, the plugin/module system (discover/init/shutdown/hot-reload).

**Phase 4 — Reflection**
- 🟡 `RTTI` — ✅ (a) identity/registry/`Object`/`Cast`/`IsA`. ✅ (b) `TypeOf`, `Variant`, `Instance`. ✅ (c) properties. ✅ (d) methods (instance/const/static, `Invoke` → `Result<Variant>`). ✅ (e) enums + `Attribute` (key→`Variant`). ✅ (f) container reflection: `ContainerInfo` + `RegisterArrayType<T>` exposes generic size/getAt/setAt over `Array<T>` via `Variant`. **Reflection arc (a–f) complete.** Remaining: external RTTI-driven serialization auto-walker (out of Core).
- ⬜ Wire serialization to RTTI; validate the whole stack with an integration test.

Each phase ships with tests and keeps Core importing cleanly on both platforms.

---

## 9. Definition of Done (Core v1)

- All modules implemented to first-pass scope; `import Raptor.Core;` builds warning-clean on Linux + Win32.
- Exceptions and C++ RTTI confirmed disabled; no STL containers in public APIs.
- Unit tests per module passing under ASan/UBSan; CI green on both platforms.
- No leaks reported by allocation tracking on test shutdown.
- A small integration target exercising memory + containers + log + math + threading + IO + library + RTTI together.

---

## 10. Deferred / Not Yet Implemented

Consolidated view of outstanding work (per-subsystem context in §8). Nothing
here blocks the implemented surface; items are refinements, platform ports, or
out-of-Core by design.

**Platform / build**
- **Win32 backends** for `System` and `Threading` — *written* (`System/Win32/Win32System.cpp`, `Threading/Win32/Win32Thread.cpp`), pending validation on Windows (cannot build in the Linux dev env).
- **CI** on Linux + Windows; verify the MSVC/Win32 module build (Phase 0 leftover).
- Stack-trace capture (`System`/`Debug`).

**Memory** — *(all done)* allocators (System/Linear/Stack/Pool/Frame), smart pointers, `TrackingAllocator` (counts + bytes + peak), `TaggedAllocator` + per-tag stats.

**Containers** — (SSO and UTF-16↔UTF-8 transcoding done.)

**Log** — in-memory ring sink; compile-time format-string checks; wide/`StringView` formatting (needs transcoding).

**Math** — SIMD (parked by request).

**IO** — endian-aware serializer wiring; async IO.

**Threading** — lock-free queues; task-graph dependencies for `JobSystem`.

**Library** — platform name/extension resolution; the plugin/module system (discover, init/shutdown lifecycle, hot-reload).

**RTTI / Serialization** — the RTTI-driven serialization auto-walker lives in an
external `Serialization` module (Core provides the contract + `Serialize` for its
own types), per §4.7.
