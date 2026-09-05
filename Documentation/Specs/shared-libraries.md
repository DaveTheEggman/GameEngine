# Shared libraries - engine as .so/.dll

Status: DESIGN (2026-09-04). Track branch: `shared-libraries`.
Goal: the engine's 179 static library targets can build as shared libraries,
with correct behavior across library boundaries. Motivating uses: real plugin
DLLs (PluginHost exists but only works today because the test plugin touches
no shared state), faster iterative links, and eventually a distributable SDK.

This doc is grounded in two full-tree audits (RTTI identity; globals + build
system). Key file:line references are inlined below.

## 1. The two real problems

Everything reduces to **identity** and **rendezvous**:

- **Identity**: type metadata compared by pointer. Each DLL gets its own copy
  of vague-linkage (inline/template) entities, so `&TypeOf<T>()` in DLL A !=
  `&TypeOf<T>()` in DLL B, and every pointer compare silently fails.
- **Rendezvous**: process-wide state reached through inline accessors with
  function-local statics. Each DLL gets its own instance, so a registry
  written by one library is empty when read from another.

What we do NOT have to solve (verified):

- No static-initializer registration anywhere; all registration is explicit
  composition roots (Pipeline.Registration, Engine.ScriptSurface, per-module
  RegisterTypes). No --whole-archive games needed.
- PIC already on globally (root CMakeLists:28).
- Cross-heap frees largely designed out by the I5 allocator idiom (owner
  carries IAllocator&; DefaultAllocator's SystemAllocator is stateless, so
  even duplicated copies free each other's blocks correctly).
- stb/cgltf/miniaudio implementation TUs are one-per-library already.

## 2. Type identity redesign (the RTTI change)

### Current state

- `TypeInfo` already carries a stable `TypeId` = FNV-1a 64 of `"ns::Name"`
  (TypeInfo.cppm:65 ComputeTypeId; Hash.cppm HashBytes). Reflected object
  types get it via RTTI_DEFINE_OBJECT; enums get it patched in EnumBuilder.
- BUT unreflected value types get `id = address of the function-local
  static` (TypeInfo.cppm:88-99) - explicitly "process-stable", which under
  DLLs becomes module-stable. All primitives (f32, bool, Array<T>, Ref<T>)
  sit in this bucket.
- Casting walks base chains with POINTER compares (Object.cppm:46-74,
  IsDerivedFrom / IsA / Cast - 328 Cast sites).
- `Instance::TryGet<T>` compares `m_type == &TypeOf<T>()`; `Variant::Is<T>`
  compares an inline-variable vtable ADDRESS (kVariantVTable<T>).
- Registries: the safe ones already key on TypeId (SerializableRegistry,
  resource factories, settings sections, content DB). The breaking ones key
  on `const TypeInfo*`: RuntimeContext subsystem locator, Scene
  m_systemsByType + component-manager routing, inspector dispatch (54
  compares), script binding (46 sites), replication (24 sites), builder
  registry.

### Decision: TypeId (the hash) becomes THE identity; pointers stay as a
### fast path within canonical metadata

1. **Compile-time ids for every type.** `TypeOf<T>()` gets its id from a
   `constexpr` hash of the type's name derived from
   `__PRETTY_FUNCTION__`/`__FUNCSIG__` (compiler-stable within one build,
   which is the only requirement - all DLLs in a process come from one
   toolchain). Reflected types keep ComputeTypeId(ns, name); REFLECT_VALUE
   continues to overwrite the id with the authored one (already stable).
   The address-derived id dies. NOTE: ids must stay stable across
   DEBUG/RelWithDebInfo and across compilers only for DISK formats - disk
   already goes through authored names (SerializationTypeId precedent in
   Component.cppm:59), so pretty-function ids are runtime-only identity and
   never serialized. Enforce that with a lint/test.
2. **Comparison by id.** IsDerivedFrom/IsA/Cast compare `t->id == base->id`
   while still walking the `base` chain (chain pointers are internally
   consistent within whichever DLL built the TypeInfo, and ids make the
   compare DLL-safe). Instance::TryGet compares ids. Variant::Is<T>
   compares `m_vtable->typeInfo()->id` (one extra indirection; the vtable
   pointer compare stays as a same-DLL fast path:
   `m_vtable == &kVariantVTable<T> || id compare`).
3. **Canonicalization through GlobalTypeRegistry.** Register() already
   dedups by id and keeps the first TypeInfo* (TypeRegistry.cppm:49). Add
   `Canonical(const TypeInfo&) -> const TypeInfo&` (FindById with fallback
   to the argument). Pointer-keyed registries (RuntimeContext, Scene
   systems map) switch their keys to TypeId - mechanical, and TypeId keys
   are hashable today.
4. **In-place mutation stays, becomes registration-gated.** REFLECT_VALUE /
   EnumBuilder / RegisterArrayType const_cast-patch `TypeOf<T>()`; under
   DLLs each library that calls the registrar patches its own copy. That is
   acceptable ONLY for the library that registers; consumers must resolve
   metadata through the registry (Canonical) rather than trusting their
   local copy's properties. Inspector/serialization already go through
   registry lookups for disk paths; audit the direct `TypeOf<T>().properties`
   reads.
   SUPERSEDED 2026-09-05 (W1 ruling, Section 5): the `TypeInfo` behind
   `TypeOf<T>()` is now PROCESS-SINGLE (`detail::TypeInfoSlot`, Core impl
   unit), so a registrar's patch is what every library reads and
   `&TypeOf<T>()` is one address per process. `Canonical()` stays as the
   belt-and-braces resolver for metadata that arrives as a by-value clone.
5. **PolymorphicElementFactory<Base> static-inline function pointers**
   (Reflection.cppm:866): audited P1 - NOT cross-library load-bearing. The
   readers are the ContainerInfo lambdas instantiated inside the same
   RegisterPolymorphicArrayType<Base> call that writes the pointers, so
   write and read are always co-located in the registering library. The
   actual cross-library gap is a consumer trusting its LOCAL
   TypeOf<Arr>().container (unpatched copy) - covered by the Canonical()
   rule, not by moving the factory.

Blast-radius numbers for planning: 314 TypeOf<> sites, 670 StaticType()
sites, 328 Cast<> sites, 54-compare inspector chain, 46 script-binding
compares, 24 replication compares. Most sites do not change (Cast's
signature is stable; only its internals change); the pointer-keyed MAPS
change (about a dozen).

## 3. Rendezvous fixes (globals)

Exactly-once state moves to the proven SAFE shape: accessor DECLARED in the
.cppm, DEFINED in the library's .cpp impl unit (the pattern GlobalLogger,
DefaultAllocator, GlobalSerializableRegistry already follow). Inventory to
de-inline (each is a one-line-per-site mechanical move):

- P0 (breaks everything): `t_pendingControl` RefCounted handshake
  (RefCounted.cppm:49) - MakeRef in DLL A parks the control block, ctor in
  DLL B must see it. Accessor function in Core's impl unit.
- Registries/singletons: Categories() (RenderData.cppm:141), Font
  Parser/Baker factories + CacheSlot (Fonts.IO/Factories.cppm),
  ScriptBackendRegistry::Get (BackendRegistry.cppm:38),
  ScriptLanguageCookRegistry (ScriptAsset.cppm:569), g_currentContext +
  ScriptCallScope (IScriptContext.cppm:162), g_globalJobs + WorkerSlot
  thread-local (JobSystem.cppm:490,456), Profiler::Get + s_local
  (Profiler.cppm:54,251), ViewId::Create counter (ViewId.cppm:55), UI
  markup/theme/SSS registries incl. the inline `static bool registered`
  guard (MarkupRegistry.cppm:359+, ThemeRegistry.cppm:27,
  UITypeRegistry.cppm:27, SSSParser.cppm:871,915), EditorRootSlot
  (Editor.Core/Context.cppm:35), MemoryTags (MemoryTag.cppm:54),
  ReflectionMutationGenerationRef (Variant.cppm:89 - per-DLL generation
  counters would let stale Variant borrows revalidate = UAF).
- Windows-only visibility work: the SAFE-shape functions (GlobalLogger,
  DefaultAllocator, GlobalTypeRegistry...) need export annotations; on ELF
  with default visibility they already resolve to one copy.

**Tripwire (required, rides P2):** clone the allocator-tripwire pattern - a
Core test that scans module interface units for `inline` + function-local
statics and namespace-scope/static-inline variables holding state, with a
pinned allowlist that also fails on stale entries.

## 4. Build system

- New `util_add_engine_library()` helper in cmake/Helpers.cmake wrapping
  add_library + ALIAS + (later) visibility props/export defines; migrate
  the 179 CMakeLists to it FIRST so every later switch is one edit.
  STATIC/SHARED selected by an `ENGINE_SHARED_LIBS` option (explicit; do
  not rely on BUILD_SHARED_LIBS which today silently no-ops).
- ELF runtime search: BUILD_RPATH/INSTALL_RPATH `$ORIGIN` on executables +
  libraries (today only DXC/wgpu dev rpaths exist). util_copy_runtime_deps
  already generalizes on Windows via TARGET_RUNTIME_DLLS.
- AngelScript double-link (Foundation/Script.AngelScript AND
  Pipeline/Script.AngelScript.Pipeline both PRIVATE-link the static
  archive): under shared libs that is two AngelScript runtimes incl. two
  thread managers. Fix: single wrapper library owns the archive (OBJECT lib
  or make the wrapper the only consumer).
- SDL3/Jolt/Luau/imgui/etc: single-consumer static links, fine as-is.
- MSVC + modules + dllexport was the highest-uncertainty item (one BMI read
  by producer and consumers; the classic FOO_API export/import macro dance
  does not transfer). RESOLVED 2026-09-04 - see the P5/W1 verdict in
  Section 5: generated `.def` for functions + `ENGINE_EXPORT_DATA` for static
  data, no import macros at all. The CORE_EXPORT / CORE_IMPORT / CORE_API
  placeholders in Prelude.h:120-138 stay unused and can be deleted.

## 5. Phases

- **P1 - stable type identity.** Compile-time ids in TypeOf<T>; id-based
  IsDerivedFrom/IsA/Cast/Instance/Variant; TypeId-keyed RuntimeContext +
  Scene maps; registry Canonical(); PolymorphicElementFactory registry.
  Tests: cross-"module" identity unit tests (two TUs simulating divergent
  copies), full battery green as static build. NO behavior change intended.
- **P2 - rendezvous.** De-inline the Section-3 inventory into impl units;
  new tripwire test with pinned allowlist.
- **P3 - build plumbing.** util_add_engine_library + 179-target migration +
  ENGINE_SHARED_LIBS option + $ORIGIN rpaths + AngelScript single-owner.
  Static build stays byte-equivalent.
- **P4 - first shared build (Linux/clang).** STATUS 2026-09-04: the flip
  WORKED FIRST TRY - build/clang-shared (ENGINE_SHARED_LIBS=ON) builds all
  179 libraries as .so with zero link errors, the full 151-suite battery
  passes ON the shared build, and Tools.Editor boots a real project linked
  against 163 engine .so files. P1-P3 cleared the ground completely (ELF
  default visibility does the rest). REMAINING P4 TAIL: (a) DONE (user
  approved 2026-09-04): the SUPPORTED PLUGIN MODEL is plugins linked against
  the SHARED engine (ENGINE_SHARED_LIBS builds) - static-engine plugins
  would re-embed Core and are explicitly unsupported (the old TestPlugin
  stays as the plugin-local-state regression). Runtime.CrossPlugin (shared
  lanes only) + a RuntimeTests case prove identity + rendezvous across a
  real dlopen boundary: TypeId-keyed host-subsystem resolution, MakeRef'd
  object crossing + host-side Cast/Release, shared GlobalTypeRegistry;
  (b) OUTPUT DIR: DONE (user ruling 2026-09-04) - ENGINE_SHARED_LIBS defaults
  BUILDSYSTEM_OUTPUT_SUFFIX to "-Shared" (the -ASAN precedent), so shared
  builds land in Bin/<cfg>/<plat>-<comp>-Shared; an explicit suffix wins; (c) a CI-able smoke lane.
- **P5 - hidden visibility + Windows.** CXX_VISIBILITY_PRESET hidden +
  VISIBILITY_INLINES_HIDDEN (converts residual duplication into link
  errors), export annotations, MSVC prototype verdict applied.

### P5 / W1 verdict - MSVC + C++20 modules + DLL (prototyped 2026-09-04)

Prototype: Foundation::Core built SHARED (Core.dll, 2415 exports) with
Core.Tests as the consumer, build/msvc-shared-proto, MSVC 14.51.36231.
Result: **the export mechanism works - Windows does NOT have to stay static** -
but one identity gap blocks W2. The slice links with **zero unresolved
symbols**, and **266 of 284 test cases pass against a shared Core** (28337
assertions). The 18 `core-reflection` cases are the exception: 16 fail, and the
first one segfaults, which aborts the process and marks the rest of a normal
run "skipped". That failure is not an export problem (see "The one real gap"
below). The MSVC **static** build is unaffected: 284/284, unchanged.

**Which hypothesis holds.** Both 1 and 2 work, and they cover *disjoint*
problems, so the shipped shape is a hybrid. Hypothesis 3 (per-library
`CORE_API` macro dance) is **not needed and should not be built** - see
"Why not per-library macros".

- **H1 - `WINDOWS_EXPORT_ALL_SYMBOLS`: fails as shipped, but is rescuable.**
  CMake's own `bindexplib` skips module-attached symbols, which carry a
  `::<!module.name>` suffix (`?Foo@@YAHXZ::<!foundation.core>`); it exported
  **0** of Core's functions. The symbols are perfectly ordinary `External`
  `notype ()` entries in the objects - only the def *generator* was blind to
  them. Replacing it with `cmake/GenerateModuleDef.cmake` (dumpbin `/symbols`
  -> filter External functions -> emit a `.def`) exports **2390** functions
  with **zero source churn**. This is the workhorse.
- **H2 - `export __declspec(dllexport)` in the module interface: works
  completely, and is the ONLY thing that works for DATA.** MSVC is
  module-aware here: the BMI records the export, and consumers reading that
  same BMI get the *import* side automatically. There is no `dllimport`
  anywhere in `Code/` (the `CORE_IMPORT` placeholder in Prelude.h:127 stays
  unused). Verified runtime-correct for virtuals, out-of-line members, static
  member functions, `thread_local` accessors, function-local statics, and
  static data members.

**The exact incantation.**

1. `cmake/GenerateModuleDef.cmake` + the `ENGINE_SHARED_LIBS` branch of
   `util_add_engine_library`: a PRE_LINK step generates `<name>_exports.def`
   and adds `/DEF:` to the link. Object paths are passed through a
   `file(GENERATE)`d list file - NOT as a command argument, because
   `COMMAND_EXPAND_LISTS` splits `$<TARGET_OBJECTS>` on `;` and the script
   then sees a single object (this silently produced 8 symbols instead of
   2252 during bring-up).
2. `ENGINE_EXPORT_DATA` (Prelude.h) = `__declspec(dllexport)` when
   `BUILDSYSTEM_SHARED_LIBS`, empty otherwise. Applied to **static DATA
   members only** - 7 sites today: Guid, Color, Color32, Float2, Float3,
   Float4, Quaternion. Functions never need it.
3. `BUILDSYSTEM_SHARED_LIBS=1` is defined **globally** on the `policy`
   interface target, not per-target. That is deliberate and load-bearing: one
   BMI is read by the definer *and* every consumer, so the macro must expand
   identically in both. A per-target define would make the definer's BMI
   disagree with the consumer's view of it.

**Why not per-library macros (`FOUNDATION_CORE_API` etc.).** The classic
Windows pattern needs a *different* expansion in the producer (dllexport) and
the consumer (dllimport) of the same header. A module interface is compiled
**once**, into one BMI that both sides read, so that split cannot be
expressed - the macro has already been baked. What makes the single-spelling
approach work is that MSVC records the export *in the BMI* and derives the
import side per consumer. Verified with a deliberate two-library test (a
second DLL consuming the first through two boundaries returned correct
values), so one global macro genuinely serves every library.

**Limits found.**

- **DATA cannot go through the `.def`.** Exporting a data symbol by name in a
  `.def` is not enough: the consumer still emits a *direct* reference rather
  than an `__imp_` indirection, because only a `dllimport` declaration
  changes the use site - and the single BMI cannot carry one. Data must use
  the `ENGINE_EXPORT_DATA` annotation. This is the whole reason both
  hypotheses are needed.
- **STL template statics reachable through a module interface break
  consumers.** `std::to_chars`/`from_chars`' *floating-point* path instantiates
  `std::_General_precision_tables_2<double>` whose static data members hit
  exactly the limit above - every consumer of a shared Core failed to link on
  them. Fix: the three float conversions now route through non-inline
  `detail::FloatToChars` / `FloatFromChars` / `FloatToCharsFixed`, defined in
  `Core/Text/TextImpl.cpp`, so the instantiation stays inside Core and no
  consumer ever references it. Integral `to_chars` has no such tables and
  stays inline. **Generalizes:** any STL facility with template-static state
  used from a module interface needs the same treatment.
- **Scale is a non-issue.** Core: 2252 unique External function symbols (1994
  module-attached), 481 data symbols - 3.4% of the 64K export cap.

**The one real gap (gates W2): templates duplicate per image.** Confirmed by
dumpbin on the consumer object:

- `GlobalTypeRegistry` -> `UNDEF` = **imported from Core.dll**. Non-template
  module-attached functions - *including `inline` ones with function-local
  statics* - are emitted once, in the owning module's TU, and consumers import
  them. Rendezvous is therefore safe **by construction** on MSVC, with no
  de-inlining required. This is stronger than ELF, which needs default
  visibility to get the same result.
- `TypeOf<Float3>` -> `SECT25E` = **the consumer emitted its own copy**.
  Templates must instantiate per-TU, so `TypeOf<T>()`'s function-local
  `TypeInfo` is **one instance per image**. `RegisterCoreTypes()` runs inside
  Core.dll and patches *Core's* copy; the executable reads its own unpatched
  copy and sees 0 properties, then dereferences the resulting null
  `PropertyInfo*`. This is not one bad test - it means **every REFLECT_*-patched
  type is invisible across a DLL boundary**, which is why 16 of the 18
  `core-reflection` cases fail (properties, methods, constants, constructors,
  enums, container access, registry-by-name). Everything else in Core - 266
  cases - is unaffected.

Linux never surfaced this because ELF unifies vague-linkage symbols across
`.so` boundaries at load time. PE has no such interposition - Windows behaves
like the hidden-visibility end state P5 is aiming at, so **it detects the gap
that Linux-shared is currently papering over**. Expect the same failures on
Linux the moment `VISIBILITY_INLINES_HIDDEN` lands.

Two candidate fixes were on the table - (a) canonicalize the ~20 metadata
readers in Reflection.cppm through `GlobalTypeRegistry().Canonical()`, or
(b) move the slot out of the template - and the Windows session recommended
(b).

**RULING 2026-09-05 (Linux session): (b), applied.** The deciding facts, from
the code rather than the failure count:

- (a) is not localized: 170 raw `TypeInfo*` compares live outside
  Reflection.cppm (replication, scene resource, both script backends, the
  inspector) and every one would need the id-compare sweep.
- (a) does not even find the right instance: `REFLECT_VALUE` patches by
  whole-struct assignment and REWRITES `id` to the authored name hash, so the
  consumer's unpatched copy still carries the signature id and an id-keyed
  `Canonical()` lookup misses it.
- (b)'s "design work" collapses once the semantics are pinned: the table is
  STATIC STORAGE (zero-initialized .bss: usable from the first call in static
  init, nothing to destroy, no allocator on the common path), the lock is a
  constant-initialized `SpinLock` taken once per (image, T) first use (the
  per-image cache is a REFERENCE, so the steady-state cost is the same
  guarded load as before), and hot reload is exactly right because a rebuilt
  module finds its slots already there: `REFLECT_VALUE` / `EnumBuilder`
  re-patch them (last registrar wins) and the slot refreshes only the
  prototype-derived layout facts (`size`/`align`).

Shape (Core/RTTI/TypeInfo.cppm + the new Core/RTTI/TypeInfoImpl.cpp):
`TypeOf<T>()` = `static TypeInfo& info = detail::TypeInfoSlot(SignatureTypeId<T>(),
ValuePrototype<T>()); return info;`. The table = an open-addressing index
(16384 entries, <= 50% load) keyed by SIGNATURE id (stable even after the
authored id is patched in) + a pool of 4096 `TypeInfo` slots, both in .bss;
past those sizes it grows onto the heap (index rehash, further pool chunks),
which is never freed - a slot must outlive its first registrar's image.
Why not the heap from the start: the first cut allocated the table lazily
and ASAN's `Runtime.Tests` reported 472 bytes leaked from `<unknown module>`
- the STATIC-lane test plugin embeds its own Core (the unsupported model,
but a test dlcloses it) and its private table was orphaned on unload; static
storage leaves nothing behind. `TypeInfo.cppm` is on the tripwire allowlist
for the per-image reference; `TypeInfoImpl.cpp` on the allocator allowlist
for the growth path (a composition root).

**Linux now models PE for this symbol.** `TypeOf<T>()` is
`COMPILER_ATTR_HIDDEN` (`visibility("hidden")` on GCC/clang, nothing on
MSVC). Proof it matters: with the attribute alone, the Linux shared lane's
Core.Tests reproduced the Windows failure EXACTLY (`core-reflection:
value-type properties are reflected` - `Properties(vec3).Size() == 0`, null
`PropertyInfo*`, SIGSEGV, 34 passed / 1 crashed); with the slot, 287/287.
The shared-lane cross-boundary test now also has the plugin hand back its own
`&TypeOf<Float3>()` (== the host's) and its property count (3) after the
HOST ran `RegisterCoreTypes()` - the W1 scenario across a real dlopen
boundary, and the plugin's `TypeOf` resolves through Core's single exported
`TypeInfoSlot` (`nm` shows it as the plugin's only reference).

Consequences: W2 is unblocked - shared Core.Tests on Windows should be
284/284 with no Windows-side change. `Object::StaticType()` needs nothing
(non-template, one definition per owning library, imported by consumers).
The unused `CORE_EXPORT`/`CORE_IMPORT`/`CORE_API` placeholders are deleted;
`COMPILER_ATTR_HIDDEN` took their place in Prelude.h. This was an
**identity** problem, not an export problem: W4's export-annotation pass
remains unnecessary.

P1 and P2 are pure wins even if shared builds never ship (they fix the
plugin path that exists today and remove latent UAF/identity traps), so
they land first and independently.

## 6. Rules established for this track

- New identity rule: runtime type identity = TypeId; TypeInfo* compares are
  allowed only as same-DLL fast paths paired with an id compare.
- Pretty-function-derived ids are RUNTIME identity only - never serialized;
  disk formats keep authored names/SerializationTypeId.
- Process-wide state lives behind non-inline accessors defined in impl
  units; the tripwire enforces it.
- A template's function-local static is ONE INSTANCE PER IMAGE on PE (and
  on ELF once hidden). It may cache a REFERENCE to impl-unit-owned state
  (`TypeOf<T>()` -> `detail::TypeInfoSlot`); it may never BE the state.
- Templates whose per-image duplication is by design carry
  `COMPILER_ATTR_HIDDEN`, so the Linux shared lane proves the rendezvous
  the way PE does instead of letting ELF interposition paper over it.

## 7. Future: game native code (researched 2026-09-04, not scheduled)

Traktor (checkout audited in full) is the precedent, and its answers converge
with this track's choices - adopt the shape, modernize the mechanisms.

### What Traktor does (verified, file:line in the audit)
- NO engine-vs-game distinction: every module (engine subsystem or
  MyGame.Shared) builds BOTH ways - shared lib in dev configs, static lib in
  ship configs; one define (T_STATIC) flips export macros AND compiles out
  the dlopen call sites. Game code is "a plugin" only because everything is.
- Dev loop: dlopen(RTLD_GLOBAL) a module-name list from settings; the game's
  module enters the list via a data-driven Feature object attached to the
  target config. No per-plugin entrypoint symbol: loaded modules are
  discovered by scanning the type registry for IRuntimePlugin / IPipeline /
  editor-factory subclasses (their registry scan = our EnumerateDerived).
- Identity: pointer-based, made safe by OUR rendezvous rule (registry =
  file-static array in Core, out-of-line EXPORTED TypeInfo ctor) - plus an
  init_seg hack, a 16384 cap, and a duplicate assert that silently corrupts
  in release. Our TypeId identity is strictly stronger.
- SHIP LINK (the Traktor pattern proper): the editor's Deploy action feeds
  an env-var contract to a Lua script that invokes link.exe/clang++ RAW.
  main() is never generated: Traktor.Runtime.App builds TWICE - executable
  in shared configs, static LIBRARY carrying main() in ship configs - and
  the deploy link is launcher.lib + engine .libs + game .lib -> branded exe.
  Static registration survives archive pruning via hand-listed
  T_FORCE_LINK_REF chains in per-module Module.cpp + /INCLUDE:__module__X.
  Gaps: Linux/macOS deploys cannot static-link at all; the Lua/env-var link
  driver is untyped and 5x duplicated ("link in reverse order" scar).
- SDK: prebuilt engine binaries under bin/latest/<plat>/<config> + engine
  source include path; games never build the engine.

### Raptor plan (adopt / modernize / skip)
- ADOPT one-artifact-kind: game native code = a module through
  util_add_engine_library, same as every engine library (already true).
- ADOPT dev = shared editor dlopens the game module (ruled model; proven by
  Runtime.CrossPlugin). ADOPT the launcher-as-library trick: build the
  player main() as a static lib in ship configs; the exporter links
  launcher + engine + game into the branded executable.
- ADOPT Feature-style data-driven module lists + headless deploy parity
  (same action classes for editor buttons and CI).
- MODERNIZE the link driver: the exporter GENERATES A SMALL CMAKE PROJECT
  and runs cmake --build (correct link order, toolchain location,
  cross-compilation, incremental relink) - never a raw linker invocation.
- MODERNIZE registration: we need NO Module.cpp/T_FORCE_LINK_REF/INCLUDE
  chains - registration is already explicit function calls, so the ship
  build's generated main simply CALLS the game's registration function
  directly. (If self-registering statics are ever wanted, use
  $<LINK_LIBRARY:WHOLE_ARCHIVE> or OBJECT libraries, not forced refs.)
- SKIP their RTLD_GLOBAL requirement: our plugins share state through the
  shared engine's exported accessors (proven under RTLD_LOCAL).
- SDK/BMI: BMIs are compiler/flag-specific and cannot ship like Traktor's
  .libs. v1 = exporter drives a cmake build against the engine checkout
  (prebuilt libs + module interface SOURCES; local BMI compile, cached).
  install(FILE_SET CXX_MODULES) is the eventual packaged-SDK path once
  CMake's support matures.
