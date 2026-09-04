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
- MSVC + modules + dllexport is the highest-uncertainty item (one BMI read
  by producer and consumers; the classic FOO_API export/import macro dance
  does not transfer). Linux/clang first; prototype the Windows story on
  Foundation::Core + one consumer before committing to it. CORE_EXPORT /
  CORE_IMPORT / CORE_API placeholders exist in Prelude.h:120-138, unused.

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
